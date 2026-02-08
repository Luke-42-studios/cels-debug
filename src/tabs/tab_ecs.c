#define _POSIX_C_SOURCE 200809L

#include "tab_ecs.h"
#include "../tui.h"
#include "../split_panel.h"
#include "../tree_view.h"
#include "../json_render.h"
#include "../scroll.h"
#include "../data_model.h"
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Per-tab private state */
typedef struct ecs_state {
    split_panel_t panel;             /* Left/right split */
    tree_view_t tree;                /* Entity tree + virtual scrolling */
    scroll_state_t inspector_scroll; /* Scroll state for component inspector */
    bool *comp_expanded;             /* Expand/collapse state per component group */
    int comp_expanded_count;         /* Number of component groups allocated */
    bool panel_created;              /* Whether split_panel windows exist */
} ecs_state_t;

/* Canonical Flecs pipeline phase execution order.
 * Keep in sync with phase_color_pair() in tree_view.c. */
static const char *PHASE_ORDER[] = {
    "OnStart", "OnLoad", "PostLoad", "PreUpdate", "OnUpdate",
    "OnValidate", "PostUpdate", "PreStore", "OnStore", "PostFrame",
};
static const int PHASE_ORDER_COUNT = 10;

/* Phase name to color pair. Duplicated from tree_view.c for inspector use.
 * Keep in sync with tree_view.c phase_color_pair(). */
static int phase_color_pair(const char *phase) {
    if (!phase) return CP_PHASE_CUSTOM;
    if (strcmp(phase, "OnLoad") == 0)      return CP_PHASE_ONLOAD;
    if (strcmp(phase, "PostLoad") == 0)    return CP_PHASE_POSTLOAD;
    if (strcmp(phase, "PreUpdate") == 0)   return CP_PHASE_PREUPDATE;
    if (strcmp(phase, "OnUpdate") == 0)    return CP_PHASE_ONUPDATE;
    if (strcmp(phase, "OnValidate") == 0)  return CP_PHASE_ONVALIDATE;
    if (strcmp(phase, "PostUpdate") == 0)  return CP_PHASE_POSTUPDATE;
    if (strcmp(phase, "PreStore") == 0)    return CP_PHASE_PRESTORE;
    if (strcmp(phase, "OnStore") == 0)     return CP_PHASE_ONSTORE;
    if (strcmp(phase, "PostFrame") == 0)   return CP_PHASE_POSTFRAME;
    if (strcmp(phase, "OnStart") == 0)     return CP_PHASE_ONLOAD;
    return CP_PHASE_CUSTOM;
}

/* --- Entity classification (CELS-C sections) --- */

/* Check if an entity has a specific tag (substring match on tag name) */
static bool has_tag(entity_node_t *node, const char *tag_name) {
    for (int i = 0; i < node->tag_count; i++) {
        if (node->tags[i] && strstr(node->tags[i], tag_name)) return true;
    }
    return false;
}

/* Check if an entity has "Component" in its component_names */
static bool has_component_component(entity_node_t *node) {
    for (int i = 0; i < node->component_count; i++) {
        if (node->component_names[i] &&
            strcmp(node->component_names[i], "Component") == 0) return true;
    }
    return false;
}

/* Extract pipeline phase from tags like "flecs.pipeline.OnLoad" -> "OnLoad" */
static char *extract_pipeline_phase(entity_node_t *node) {
    for (int i = 0; i < node->tag_count; i++) {
        if (node->tags[i] && strncmp(node->tags[i], "flecs.pipeline.", 15) == 0) {
            const char *phase = node->tags[i] + 15;
            if (phase[0] != '\0') return strdup(phase);
        }
    }
    return NULL;
}

/* Check if entity is a user-defined lifecycle controller.
 * Matches CEL_Lifecycle macro names: "MainMenuLC", "SettingsLC", "MenuCycle", etc.
 * Does NOT match CELS_LifecycleSystem (that's a system, not a lifecycle). */
static bool name_is_lifecycle(entity_node_t *node) {
    if (!node->name) return false;
    size_t nlen = strlen(node->name);
    if (nlen >= 2 && strcmp(node->name + nlen - 2, "LC") == 0) return true;
    if (nlen >= 5 && strcmp(node->name + nlen - 5, "Cycle") == 0) return true;
    if (strncmp(node->name, "lifecycle_", 10) == 0) return true;
    return false;
}

/* Classify a single node (root-level only -- children inherit) */
static entity_class_t classify_node(entity_node_t *node) {
    free(node->class_detail);
    node->class_detail = NULL;

    /* Systems -- flecs.system.System tag, observers */
    if (has_tag(node, "flecs.system.System")) {
        node->class_detail = extract_pipeline_phase(node);
        if (!node->class_detail) node->class_detail = strdup("System");
        return ENTITY_CLASS_SYSTEM;
    }
    if (has_tag(node, "flecs.core.Observer")) {
        node->class_detail = strdup("Observer");
        return ENTITY_CLASS_SYSTEM;
    }
    /* L: Lifecycles -- user-defined lifecycle entities (after system check so
       CELS_LifecycleSystem stays classified as a system) */
    if (name_is_lifecycle(node)) {
        return ENTITY_CLASS_LIFECYCLE;
    }
    /* Components -- component type entities */
    if (has_component_component(node)) {
        return ENTITY_CLASS_COMPONENT;
    }
    /* E: Entities -- leaf scene entities (no children, have component data) */
    if (node->child_count == 0 && node->component_count > 0) {
        return ENTITY_CLASS_ENTITY;
    }
    /* C: Compositions -- parent entities (have children = scene structure) */
    return ENTITY_CLASS_COMPOSITION;
}

/* Propagate a class to all descendants */
static void propagate_class(entity_node_t *node, entity_class_t cls) {
    node->entity_class = cls;
    for (int i = 0; i < node->child_count; i++) {
        propagate_class(node->children[i], cls);
    }
}

/* Classify all roots in entity list -- children inherit root's class */
static void classify_all_entities(entity_list_t *list) {
    if (!list) return;
    for (int i = 0; i < list->root_count; i++) {
        entity_class_t cls = classify_node(list->roots[i]);
        propagate_class(list->roots[i], cls);
    }
}

/* --- Annotation: enrich component entities with registry data --- */

static void annotate_component_entities(entity_list_t *list,
                                         component_registry_t *reg) {
    if (!list || !reg) return;
    for (int i = 0; i < list->root_count; i++) {
        entity_node_t *node = list->roots[i];
        if (node->entity_class != ENTITY_CLASS_COMPONENT) continue;
        if (!node->name) continue;

        /* Find matching registry entry */
        for (int r = 0; r < reg->count; r++) {
            if (reg->components[r].name &&
                strcmp(reg->components[r].name, node->name) == 0) {
                free(node->class_detail);
                char buf[64];
                if (reg->components[r].has_type_info && reg->components[r].size > 0) {
                    snprintf(buf, sizeof(buf), "%d entities, %dB",
                             reg->components[r].entity_count,
                             reg->components[r].size);
                } else {
                    snprintf(buf, sizeof(buf), "%d entities",
                             reg->components[r].entity_count);
                }
                node->class_detail = strdup(buf);
                break;
            }
        }
    }
}

/* --- Enrichment: merge pipeline stats into system entity nodes --- */

/* Enrich system entity nodes with data from pipeline stats.
 * Matches by leaf name (fast, sufficient for small apps).
 * Also builds the phase list for tree_view phase sub-headers. */
static void enrich_systems_with_pipeline(entity_list_t *list,
                                          system_registry_t *reg,
                                          tree_view_t *tree) {
    if (!list || !reg || reg->count == 0) {
        tree_view_set_phases(tree, NULL, NULL, 0);
        return;
    }

    /* Build ordered phase list from system entities (using entity tags) */
    char *found_phases[32];
    int found_counts[32];
    int found_count = 0;

    for (int i = 0; i < list->root_count; i++) {
        entity_node_t *node = list->roots[i];
        if (node->entity_class != ENTITY_CLASS_SYSTEM) continue;
        if (!node->class_detail) continue;

        bool exists = false;
        for (int p = 0; p < found_count; p++) {
            if (strcmp(found_phases[p], node->class_detail) == 0) {
                found_counts[p]++;
                exists = true;
                break;
            }
        }
        if (!exists && found_count < 32) {
            found_phases[found_count] = node->class_detail;
            found_counts[found_count] = 1;
            found_count++;
        }
    }

    /* Sort phases by canonical execution order (insertion sort, max 32) */
    for (int i = 1; i < found_count; i++) {
        char *phase = found_phases[i];
        int count = found_counts[i];
        int order_i = PHASE_ORDER_COUNT;
        for (int k = 0; k < PHASE_ORDER_COUNT; k++) {
            if (strcmp(phase, PHASE_ORDER[k]) == 0) { order_i = k; break; }
        }
        int j = i - 1;
        while (j >= 0) {
            int order_j = PHASE_ORDER_COUNT;
            for (int k = 0; k < PHASE_ORDER_COUNT; k++) {
                if (strcmp(found_phases[j], PHASE_ORDER[k]) == 0) { order_j = k; break; }
            }
            if (order_j > order_i) {
                found_phases[j + 1] = found_phases[j];
                found_counts[j + 1] = found_counts[j];
                j--;
            } else break;
        }
        found_phases[j + 1] = phase;
        found_counts[j + 1] = count;
    }

    tree_view_set_phases(tree, found_phases, found_counts, found_count);

    /* Enrich each system entity with pipeline stats (match by leaf name) */
    for (int i = 0; i < list->root_count; i++) {
        entity_node_t *node = list->roots[i];
        if (node->entity_class != ENTITY_CLASS_SYSTEM) continue;
        if (!node->name) continue;

        for (int s = 0; s < reg->count; s++) {
            if (reg->systems[s].name &&
                strcmp(reg->systems[s].name, node->name) == 0) {
                node->system_match_count = reg->systems[s].matched_entity_count;
                node->disabled = reg->systems[s].disabled;
                break;
            }
        }
    }
}

/* --- Helper: count inspector content rows for scroll total --- */

static int count_inspector_rows(const entity_detail_t *detail,
                                const bool *expanded, int expanded_count) {
    int rows = 0;

    if (detail->components && yyjson_is_obj(detail->components)) {
        size_t idx, max;
        yyjson_val *key, *val;
        int comp_idx = 0;
        yyjson_obj_foreach(detail->components, idx, max, key, val) {
            if (is_hidden_component(yyjson_get_str(key))) continue;
            rows++;
            bool exp = (comp_idx < expanded_count) ? expanded[comp_idx] : true;
            if (exp && val && !yyjson_is_null(val)) {
                if (yyjson_is_obj(val))
                    rows += (int)yyjson_obj_size(val);
                else if (yyjson_is_arr(val))
                    rows += (int)yyjson_arr_size(val);
                else
                    rows += 1;
            }
            comp_idx++;
        }
    }

    if (detail->tags && yyjson_is_arr(detail->tags) && yyjson_arr_size(detail->tags) > 0) {
        rows++;
        rows += (int)yyjson_arr_size(detail->tags);
    }

    if (detail->pairs && yyjson_is_obj(detail->pairs) && yyjson_obj_size(detail->pairs) > 0) {
        rows++;
        rows += (int)yyjson_obj_size(detail->pairs);
    }

    return rows;
}

/* --- Helper: ensure comp_expanded array matches component count --- */

static void ensure_comp_expanded(ecs_state_t *es, int needed) {
    if (es->comp_expanded_count >= needed) return;

    bool *new_arr = realloc(es->comp_expanded, (size_t)needed * sizeof(bool));
    if (!new_arr) return;

    for (int i = es->comp_expanded_count; i < needed; i++) {
        new_arr[i] = true;
    }
    es->comp_expanded = new_arr;
    es->comp_expanded_count = needed;
}

/* --- Helper: count total component/tag/pair groups for expand tracking --- */

static int count_groups(const entity_detail_t *detail) {
    int count = 0;
    if (detail->components && yyjson_is_obj(detail->components)) {
        count += (int)yyjson_obj_size(detail->components);
    }
    if (detail->tags && yyjson_is_arr(detail->tags) && yyjson_arr_size(detail->tags) > 0) {
        count++;
    }
    if (detail->pairs && yyjson_is_obj(detail->pairs) && yyjson_obj_size(detail->pairs) > 0) {
        count++;
    }
    return count;
}

/* --- Helper: find which group index corresponds to the current inspector cursor --- */

static int cursor_to_group_index(const entity_detail_t *detail,
                                 const bool *expanded, int expanded_count,
                                 int cursor_row) {
    int row = 0;
    int group_idx = 0;

    if (detail->components && yyjson_is_obj(detail->components)) {
        size_t idx, max;
        yyjson_val *key, *val;
        yyjson_obj_foreach(detail->components, idx, max, key, val) {
            if (is_hidden_component(yyjson_get_str(key))) continue;
            if (row == cursor_row) return group_idx;
            row++;
            bool exp = (group_idx < expanded_count) ? expanded[group_idx] : true;
            if (exp && val && !yyjson_is_null(val)) {
                if (yyjson_is_obj(val))
                    row += (int)yyjson_obj_size(val);
                else if (yyjson_is_arr(val))
                    row += (int)yyjson_arr_size(val);
                else
                    row += 1;
            }
            group_idx++;
        }
    }

    if (detail->tags && yyjson_is_arr(detail->tags) && yyjson_arr_size(detail->tags) > 0) {
        if (row == cursor_row) return group_idx;
        row++;
        row += (int)yyjson_arr_size(detail->tags);
        group_idx++;
    }

    if (detail->pairs && yyjson_is_obj(detail->pairs) && yyjson_obj_size(detail->pairs) > 0) {
        if (row == cursor_row) return group_idx;
        group_idx++;
    }

    return -1;
}

/* --- Helper: find system_info_t by name in system_registry --- */

static system_info_t *find_system_info(const app_state_t *state,
                                        const char *name) {
    if (!state->system_registry || !name) return NULL;
    for (int i = 0; i < state->system_registry->count; i++) {
        if (state->system_registry->systems[i].name &&
            strcmp(state->system_registry->systems[i].name, name) == 0) {
            return &state->system_registry->systems[i];
        }
    }
    return NULL;
}

/* --- Helper: find entities with overlapping components (approximation) --- */

/* Find entities that share components with this system's access pattern.
 * This is an APPROXIMATION -- the Flecs REST API does not expose the exact
 * query expression, so we match entities that have at least one non-flecs
 * component from the system's component list. Not all returned entities
 * are necessarily matched by the system's actual query.
 * Returns array of entity_node_t pointers. Caller frees the array (not nodes). */
static entity_node_t **build_system_matches(const entity_detail_t *sys_detail,
                                             const entity_list_t *elist,
                                             int *out_count) {
    *out_count = 0;
    if (!sys_detail || !elist || !sys_detail->components) return NULL;

    /* Collect system's non-internal component names */
    int comp_count = 0;
    const char *comp_names[64];
    if (yyjson_is_obj(sys_detail->components)) {
        size_t ci, cmax;
        yyjson_val *ckey, *cval;
        yyjson_obj_foreach(sys_detail->components, ci, cmax, ckey, cval) {
            if (comp_count < 64) {
                comp_names[comp_count++] = yyjson_get_str(ckey);
            }
        }
    }
    if (comp_count == 0) return NULL;

    /* Filter out system-internal components */
    const char *query_comps[64];
    int query_count = 0;
    for (int i = 0; i < comp_count; i++) {
        if (!comp_names[i]) continue;
        if (strncmp(comp_names[i], "flecs.", 6) == 0) continue;
        if (strcmp(comp_names[i], "Component") == 0) continue;
        query_comps[query_count++] = comp_names[i];
    }
    if (query_count == 0) return NULL;

    /* Find entities with at least one overlapping component */
    entity_node_t **matches = malloc((size_t)elist->count * sizeof(entity_node_t *));
    if (!matches) return NULL;
    int match_count = 0;

    for (int i = 0; i < elist->count; i++) {
        entity_node_t *node = elist->nodes[i];
        if (node->entity_class == ENTITY_CLASS_SYSTEM) continue;
        if (node->entity_class == ENTITY_CLASS_COMPONENT) continue;

        bool has_match = false;
        for (int q = 0; q < query_count && !has_match; q++) {
            for (int c = 0; c < node->component_count; c++) {
                if (node->component_names[c] &&
                    strcmp(node->component_names[c], query_comps[q]) == 0) {
                    has_match = true;
                    break;
                }
            }
        }
        if (has_match) {
            matches[match_count++] = node;
        }
    }

    *out_count = match_count;
    return matches;
}

/* --- Inspector: system detail (system entity selected) --- */

static void draw_system_detail(WINDOW *rwin, int rh, int rw,
                                entity_node_t *sel,
                                const app_state_t *state,
                                ecs_state_t *es) {
    int row = 1;

    /* Title: system name */
    wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
    mvwprintw(rwin, row, 1, "%.*s", rw, sel->name ? sel->name : "(unnamed)");
    wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
    row++;

    /* Separator */
    wattron(rwin, A_DIM);
    wmove(rwin, row, 1);
    for (int x = 0; x < rw; x++) waddch(rwin, ACS_HLINE);
    wattroff(rwin, A_DIM);
    row++;

    /* Metadata */
    system_info_t *sinfo = find_system_info(state, sel->name);

    /* Phase */
    row++;
    wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
    mvwprintw(rwin, row, 2, "Phase");
    wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
    if (sel->class_detail) {
        int cp = phase_color_pair(sel->class_detail);
        wattron(rwin, COLOR_PAIR(cp) | A_BOLD);
        mvwprintw(rwin, row, 16, "%s", sel->class_detail);
        wattroff(rwin, COLOR_PAIR(cp) | A_BOLD);
    } else {
        wattron(rwin, A_DIM);
        mvwprintw(rwin, row, 16, "Unknown");
        wattroff(rwin, A_DIM);
    }

    /* Status */
    row++;
    wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
    mvwprintw(rwin, row, 2, "Status");
    wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
    if (sel->disabled) {
        wattron(rwin, COLOR_PAIR(CP_DISCONNECTED));
        mvwprintw(rwin, row, 16, "Disabled");
        wattroff(rwin, COLOR_PAIR(CP_DISCONNECTED));
    } else {
        wattron(rwin, COLOR_PAIR(CP_CONNECTED));
        mvwprintw(rwin, row, 16, "Enabled");
        wattroff(rwin, COLOR_PAIR(CP_CONNECTED));
    }

    /* Match count */
    row++;
    wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
    mvwprintw(rwin, row, 2, "Matched");
    wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
    wattron(rwin, COLOR_PAIR(CP_JSON_NUMBER));
    mvwprintw(rwin, row, 16, "%d entities", sel->system_match_count);
    wattroff(rwin, COLOR_PAIR(CP_JSON_NUMBER));

    /* Timing */
    if (sinfo && sinfo->time_spent_ms > 0.0) {
        row++;
        wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
        mvwprintw(rwin, row, 2, "Time");
        wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
        wattron(rwin, COLOR_PAIR(CP_JSON_NUMBER));
        mvwprintw(rwin, row, 16, "%.2fms", sinfo->time_spent_ms);
        wattroff(rwin, COLOR_PAIR(CP_JSON_NUMBER));
    }

    /* Table count */
    if (sinfo && sinfo->matched_table_count > 0) {
        row++;
        wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
        mvwprintw(rwin, row, 2, "Tables");
        wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
        wattron(rwin, COLOR_PAIR(CP_JSON_NUMBER));
        mvwprintw(rwin, row, 16, "%d", sinfo->matched_table_count);
        wattroff(rwin, COLOR_PAIR(CP_JSON_NUMBER));
    }

    /* Full path */
    if (sinfo && sinfo->full_path) {
        row++;
        wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
        mvwprintw(rwin, row, 2, "Path");
        wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
        wattron(rwin, A_DIM);
        mvwprintw(rwin, row, 16, "%.*s", rw - 16, sinfo->full_path);
        wattroff(rwin, A_DIM);
    }

    /* Description (from CEL_Doc) */
    if (state->entity_detail && sel->full_path &&
        strcmp(state->entity_detail->path, sel->full_path) == 0 &&
        state->entity_detail->doc_brief) {
        row++;
        wattron(rwin, A_DIM);
        const char *line_start = state->entity_detail->doc_brief;
        while (*line_start) {
            const char *line_end = strchr(line_start, '\n');
            int line_len = line_end ? (int)(line_end - line_start) : (int)strlen(line_start);
            if (line_len > rw - 4) line_len = rw - 4;
            if (row < rh) {
                mvwprintw(rwin, row, 2, "%.*s", line_len, line_start);
                row++;
            }
            if (line_end) line_start = line_end + 1;
            else break;
        }
        wattroff(rwin, A_DIM);
    }

    /* Component access list from entity detail */
    if (state->entity_detail && sel->full_path &&
        strcmp(state->entity_detail->path, sel->full_path) == 0) {
        if (state->entity_detail->components &&
            yyjson_is_obj(state->entity_detail->components)) {
            row += 2;
            wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
            mvwprintw(rwin, row, 1, "Component Access");
            wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
            row++;

            size_t ci, cmax;
            yyjson_val *ckey, *cval;
            yyjson_obj_foreach(state->entity_detail->components, ci, cmax, ckey, cval) {
                if (is_hidden_component(yyjson_get_str(ckey))) continue;
                if (row >= rh) break;
                wattron(rwin, COLOR_PAIR(CP_JSON_STRING));
                mvwprintw(rwin, row, 3, "%.*s", rw - 4, yyjson_get_str(ckey));
                wattroff(rwin, COLOR_PAIR(CP_JSON_STRING));
                row++;
            }
        }
    }

    /* Approximate matched entities section (scrollable) */
    row += 1;
    int match_header_row = row;
    if (match_header_row < rh) {
        wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
        mvwprintw(rwin, match_header_row, 1, "Matched Entities");
        wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);

        /* Note: this is a component overlap approximation */
        wattron(rwin, A_DIM);
        mvwprintw(rwin, match_header_row, 19, "(approx)");
        wattroff(rwin, A_DIM);

        /* Build approximate match list from entity detail component access */
        int match_count = 0;
        entity_node_t **matches = NULL;
        if (state->entity_detail && sel->full_path &&
            strcmp(state->entity_detail->path, sel->full_path) == 0) {
            matches = build_system_matches(state->entity_detail,
                                            state->entity_list, &match_count);
        }

        int avail_rows = rh - (match_header_row + 1);
        if (avail_rows < 1) avail_rows = 1;

        es->inspector_scroll.total_items = match_count;
        es->inspector_scroll.visible_rows = avail_rows;
        scroll_ensure_visible(&es->inspector_scroll);

        if (match_count > 0 && matches) {
            for (int r = 0; r < avail_rows; r++) {
                int idx = es->inspector_scroll.scroll_offset + r;
                if (idx >= match_count) break;

                int disp_row = match_header_row + 1 + r;
                entity_node_t *ent = matches[idx];
                bool is_cursor = (idx == es->inspector_scroll.cursor &&
                                  es->panel.focus == 1);

                if (is_cursor) wattron(rwin, A_REVERSE);

                wmove(rwin, disp_row, 1);
                for (int c = 0; c < rw; c++) waddch(rwin, ' ');

                const char *dname = ent->name;
                char id_buf[32];
                if (!dname || dname[0] == '\0') {
                    snprintf(id_buf, sizeof(id_buf), "#%lu",
                             (unsigned long)ent->id);
                    dname = id_buf;
                }

                wattron(rwin, COLOR_PAIR(CP_ENTITY_NAME));
                mvwprintw(rwin, disp_row, 2, "%.*s", rw / 2, dname);
                wattroff(rwin, COLOR_PAIR(CP_ENTITY_NAME));

                if (ent->full_path) {
                    int name_end = getcurx(rwin);
                    int avail = rw - (name_end - 1);
                    if (avail > 2) {
                        wattron(rwin, A_DIM);
                        mvwprintw(rwin, disp_row, name_end + 1, "%.*s",
                                  avail - 2, ent->full_path);
                        wattroff(rwin, A_DIM);
                    }
                }

                if (is_cursor) wattroff(rwin, A_REVERSE);
            }
        } else {
            int disp_row = match_header_row + 1;
            if (disp_row < rh) {
                wattron(rwin, A_DIM);
                if (!state->entity_detail || !sel->full_path ||
                    strcmp(state->entity_detail->path, sel->full_path) != 0) {
                    mvwprintw(rwin, disp_row, 3, "Loading...");
                } else {
                    mvwprintw(rwin, disp_row, 3, "No matches (task system)");
                }
                wattroff(rwin, A_DIM);
            }
        }

        free(matches);
    }
}

/* --- Helper: update selected_entity_path from tree cursor --- */

static void sync_selected_path(ecs_state_t *es, app_state_t *state) {
    entity_node_t *sel = tree_view_selected(&es->tree);
    free(state->selected_entity_path);
    state->selected_entity_path = sel ? strdup(sel->full_path) : NULL;

    if (state->entity_detail && sel &&
        strcmp(state->entity_detail->path, sel->full_path) != 0) {
        entity_detail_free(state->entity_detail);
        state->entity_detail = NULL;
    }
}

/* --- Helper: cross-navigate from inspector to entity in tree --- */

static bool cross_navigate_to_entity(ecs_state_t *es, app_state_t *state,
                                      const char *entity_path) {
    if (!entity_path || !state->entity_list) return false;

    /* 1. Ensure Entities section is expanded */
    es->tree.section_collapsed[ENTITY_CLASS_ENTITY] = false;

    /* 2. Rebuild visible rows to include Entities section items */
    tree_view_rebuild_visible(&es->tree, state->entity_list);

    /* 3. Find the target entity in the display list */
    for (int i = 0; i < es->tree.row_count; i++) {
        entity_node_t *node = es->tree.rows[i].node;
        if (node && node->full_path &&
            strcmp(node->full_path, entity_path) == 0) {
            es->tree.scroll.cursor = i;
            scroll_ensure_visible(&es->tree.scroll);

            /* 4. Update selected path for detail polling */
            free(state->selected_entity_path);
            state->selected_entity_path = strdup(entity_path);

            /* 5. Switch focus to left panel */
            es->panel.focus = 0;
            return true;
        }
    }

    /* Try expanding Compositions section too */
    es->tree.section_collapsed[ENTITY_CLASS_COMPOSITION] = false;
    tree_view_rebuild_visible(&es->tree, state->entity_list);

    for (int i = 0; i < es->tree.row_count; i++) {
        entity_node_t *node = es->tree.rows[i].node;
        if (node && node->full_path &&
            strcmp(node->full_path, entity_path) == 0) {
            es->tree.scroll.cursor = i;
            scroll_ensure_visible(&es->tree.scroll);
            free(state->selected_entity_path);
            state->selected_entity_path = strdup(entity_path);
            es->panel.focus = 0;
            return true;
        }
    }

    /* Not found -- show footer message */
    free(state->footer_message);
    state->footer_message = strdup("Entity not found");
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    state->footer_message_expire = ts.tv_sec * 1000 + ts.tv_nsec / 1000000 + 3000;
    return false;
}

/* --- Inspector: pipeline visualization (phase sub-header selected) --- */

static void draw_pipeline_viz(WINDOW *rwin, int rh, int rw,
                               tree_view_t *tree, int selected_phase,
                               const app_state_t *state) {
    /* Title */
    wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
    mvwprintw(rwin, 1, 1, "Pipeline Execution Order");
    wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);

    /* Separator */
    wattron(rwin, A_DIM);
    wmove(rwin, 2, 1);
    for (int x = 0; x < rw; x++) waddch(rwin, ACS_HLINE);
    wattroff(rwin, A_DIM);

    int row = 4;
    #define PIPE_VERT  "\xe2\x94\x82"
    #define PIPE_ARROW "\xe2\x86\x93"

    double total_time = 0.0;
    int total_systems = 0;

    for (int p = 0; p < tree->phase_count; p++) {
        if (row >= rh) break;

        bool is_selected = (p == selected_phase);
        int cp = phase_color_pair(tree->phase_names[p]);

        if (is_selected) wattron(rwin, A_REVERSE);

        /* Phase name in color */
        wattron(rwin, COLOR_PAIR(cp) | A_BOLD);
        mvwprintw(rwin, row, 3, "%-14s", tree->phase_names[p]);
        wattroff(rwin, COLOR_PAIR(cp) | A_BOLD);

        /* System count */
        int sys_count = tree->phase_system_counts[p];
        wprintw(rwin, " %d system%s", sys_count, sys_count == 1 ? "" : "s");
        total_systems += sys_count;

        /* Timing: sum time_spent_ms for systems in this phase */
        if (state->system_registry && state->entity_list) {
            double phase_time = 0.0;
            for (int s = 0; s < state->system_registry->count; s++) {
                system_info_t *si = &state->system_registry->systems[s];
                /* Find entity node for this system to check its phase */
                for (int e = 0; e < state->entity_list->root_count; e++) {
                    entity_node_t *en = state->entity_list->roots[e];
                    if (en->entity_class == ENTITY_CLASS_SYSTEM &&
                        en->name && si->name &&
                        strcmp(en->name, si->name) == 0 &&
                        en->class_detail &&
                        strcmp(en->class_detail, tree->phase_names[p]) == 0) {
                        phase_time += si->time_spent_ms;
                        break;
                    }
                }
            }
            if (phase_time > 0.0) {
                wprintw(rwin, "   %.1fms", phase_time);
                total_time += phase_time;
            }
        }

        if (is_selected) wattroff(rwin, A_REVERSE);
        row++;

        /* Draw connector to next phase */
        if (p < tree->phase_count - 1 && row + 1 < rh) {
            wattron(rwin, A_DIM);
            mvwprintw(rwin, row, 6, PIPE_VERT);
            row++;
            mvwprintw(rwin, row, 6, PIPE_ARROW);
            row++;
            wattroff(rwin, A_DIM);
        }
    }

    /* Total summary */
    row += 1;
    if (row < rh) {
        wattron(rwin, A_DIM);
        wmove(rwin, row, 1);
        for (int x = 0; x < rw; x++) waddch(rwin, ACS_HLINE);
        wattroff(rwin, A_DIM);
        row++;
    }
    if (row < rh) {
        mvwprintw(rwin, row, 3, "Total: %d system%s",
                  total_systems, total_systems == 1 ? "" : "s");
        if (total_time > 0.0) {
            wprintw(rwin, ", %.1fms/frame", total_time);
        }
    }
}

/* --- Inspector: systems summary (Systems section header selected) --- */

static void draw_systems_summary(WINDOW *rwin, int rh, int rw,
                                  const app_state_t *state) {
    /* Title */
    wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
    mvwprintw(rwin, 1, 1, "Systems Overview");
    wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);

    /* Separator */
    wattron(rwin, A_DIM);
    wmove(rwin, 2, 1);
    for (int x = 0; x < rw; x++) waddch(rwin, ACS_HLINE);
    wattroff(rwin, A_DIM);

    if (!state->entity_list) {
        wattron(rwin, A_DIM);
        mvwprintw(rwin, 4, 3, "Waiting for data...");
        wattroff(rwin, A_DIM);
        return;
    }

    /* Count totals */
    int total = 0, enabled = 0, disabled = 0;
    for (int i = 0; i < state->entity_list->root_count; i++) {
        entity_node_t *n = state->entity_list->roots[i];
        if (n->entity_class != ENTITY_CLASS_SYSTEM) continue;
        total++;
        if (n->disabled) disabled++;
        else enabled++;
    }

    int row = 4;
    wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
    mvwprintw(rwin, row, 3, "Total Systems");
    wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
    wattron(rwin, COLOR_PAIR(CP_JSON_NUMBER));
    mvwprintw(rwin, row, 20, "%d", total);
    wattroff(rwin, COLOR_PAIR(CP_JSON_NUMBER));

    row++;
    wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
    mvwprintw(rwin, row, 3, "Enabled");
    wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
    wattron(rwin, COLOR_PAIR(CP_CONNECTED));
    mvwprintw(rwin, row, 20, "%d", enabled);
    wattroff(rwin, COLOR_PAIR(CP_CONNECTED));

    row++;
    wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
    mvwprintw(rwin, row, 3, "Disabled");
    wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
    if (disabled > 0) {
        wattron(rwin, COLOR_PAIR(CP_DISCONNECTED));
        mvwprintw(rwin, row, 20, "%d", disabled);
        wattroff(rwin, COLOR_PAIR(CP_DISCONNECTED));
    } else {
        wattron(rwin, A_DIM);
        mvwprintw(rwin, row, 20, "0");
        wattroff(rwin, A_DIM);
    }

    /* Phase distribution */
    row += 2;
    wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
    mvwprintw(rwin, row, 1, "Phase Distribution");
    wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
    row++;

    char *phases[32];
    int pcounts[32];
    int pcount = 0;

    for (int i = 0; i < state->entity_list->root_count; i++) {
        entity_node_t *n = state->entity_list->roots[i];
        if (n->entity_class != ENTITY_CLASS_SYSTEM) continue;
        const char *ph = n->class_detail ? n->class_detail : "Unknown";
        bool found = false;
        for (int p = 0; p < pcount; p++) {
            if (strcmp(phases[p], ph) == 0) { pcounts[p]++; found = true; break; }
        }
        if (!found && pcount < 32) {
            phases[pcount] = (char *)ph;
            pcounts[pcount] = 1;
            pcount++;
        }
    }

    for (int p = 0; p < pcount && row < rh; p++) {
        int cp = phase_color_pair(phases[p]);
        wattron(rwin, COLOR_PAIR(cp));
        mvwprintw(rwin, row, 3, "%-14s", phases[p]);
        wattroff(rwin, COLOR_PAIR(cp));
        wprintw(rwin, " %d", pcounts[p]);
        row++;
    }
}

/* --- Lifecycle --- */

void tab_ecs_init(tab_t *self) {
    ecs_state_t *es = calloc(1, sizeof(ecs_state_t));
    if (!es) return;

    tree_view_init(&es->tree);
    scroll_reset(&es->inspector_scroll);
    es->panel_created = false;
    es->comp_expanded = NULL;
    es->comp_expanded_count = 0;

    self->state = es;
}

void tab_ecs_fini(tab_t *self) {
    ecs_state_t *es = (ecs_state_t *)self->state;
    if (!es) return;

    if (es->panel_created) {
        split_panel_destroy(&es->panel);
    }
    tree_view_fini(&es->tree);
    free(es->comp_expanded);
    free(es);
    self->state = NULL;
}

/* --- Draw --- */

void tab_ecs_draw(const tab_t *self, WINDOW *win, const void *app_state) {
    ecs_state_t *es = (ecs_state_t *)self->state;
    if (!es) return;

    const app_state_t *state = (const app_state_t *)app_state;

    int h = getmaxy(win);
    int w = getmaxx(win);

    if (!es->panel_created) {
        split_panel_create(&es->panel, h, w, getbegy(win));
        es->panel_created = true;
    } else if (h != es->panel.height ||
               w != es->panel.left_width + es->panel.right_width) {
        split_panel_resize(&es->panel, h, w, getbegy(win));
    }

    werase(es->panel.left);
    werase(es->panel.right);
    split_panel_draw_borders(&es->panel, "ECS", "Inspector");

    /* --- Left panel: entity tree --- */
    if (state->entity_list) {
        /* Classify entities into CELS-C sections */
        classify_all_entities(state->entity_list);

        /* Annotate component entities with registry data (entity count, size) */
        annotate_component_entities(state->entity_list, state->component_registry);

        /* Enrich system entities with pipeline stats + build phase grouping */
        enrich_systems_with_pipeline(state->entity_list, state->system_registry, &es->tree);

        tree_view_rebuild_visible(&es->tree, state->entity_list);

        /* Auto-select first entity if nothing selected yet */
        if (!state->selected_entity_path && es->tree.row_count > 0) {
            for (int i = 0; i < es->tree.row_count; i++) {
                entity_node_t *n = es->tree.rows[i].node;
                if (n && n->full_path) {
                    app_state_t *mut_state = (app_state_t *)state;
                    mut_state->selected_entity_path = strdup(n->full_path);
                    break;
                }
            }
        }

        tree_view_render(&es->tree, es->panel.left);
    } else {
        const char *msg = "Waiting for data...";
        int msg_len = (int)strlen(msg);
        int max_y = getmaxy(es->panel.left);
        int max_x = getmaxx(es->panel.left);
        wattron(es->panel.left, A_DIM);
        mvwprintw(es->panel.left, max_y / 2, (max_x - msg_len) / 2, "%s", msg);
        wattroff(es->panel.left, A_DIM);
    }

    /* --- Right panel: context-sensitive inspector --- */
    entity_node_t *sel = tree_view_selected(&es->tree);
    display_row_t *cur_row = NULL;
    if (es->tree.rows && es->tree.scroll.cursor >= 0 &&
        es->tree.scroll.cursor < es->tree.row_count) {
        cur_row = &es->tree.rows[es->tree.scroll.cursor];
    }
    WINDOW *rwin = es->panel.right;
    int rh = getmaxy(rwin) - 2;
    int rw = getmaxx(rwin) - 2;

    if (cur_row && !cur_row->node && cur_row->section_idx == ENTITY_CLASS_SYSTEM) {
        if (cur_row->phase_group >= 0) {
            /* --- Branch: Phase sub-header selected -> Pipeline visualization --- */
            draw_pipeline_viz(rwin, rh, rw, &es->tree, cur_row->phase_group, state);
        } else {
            /* --- Branch: Systems section header selected -> Summary stats --- */
            draw_systems_summary(rwin, rh, rw, state);
        }
    } else if (sel && sel->entity_class == ENTITY_CLASS_SYSTEM) {
        /* --- Branch: System entity selected -> System detail + matched entities --- */
        draw_system_detail(rwin, rh, rw, sel, state, es);
    } else if (sel && sel->entity_class == ENTITY_CLASS_COMPONENT) {
        /* --- Branch A: Component type selected -- show entities with this component --- */
        const char *comp_name = sel->name;

        if (comp_name && state->entity_list && state->entity_list->count > 0) {
            entity_list_t *elist = state->entity_list;

            /* Filter entities that have the selected component */
            int match_cap = elist->count;
            entity_node_t **matches = malloc((size_t)match_cap * sizeof(entity_node_t *));
            int match_count = 0;

            if (matches) {
                for (int i = 0; i < elist->count; i++) {
                    entity_node_t *node = elist->nodes[i];
                    for (int c = 0; c < node->component_count; c++) {
                        if (node->component_names[c] &&
                            strcmp(node->component_names[c], comp_name) == 0) {
                            matches[match_count++] = node;
                            break;
                        }
                    }
                }

                /* Header */
                wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
                mvwprintw(rwin, 1, 1, "Entities with %.*s", rw - 14, comp_name);
                wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);

                /* Update scroll state for entity list (below header) */
                es->inspector_scroll.total_items = match_count;
                es->inspector_scroll.visible_rows = rh - 1; /* minus header row */
                scroll_ensure_visible(&es->inspector_scroll);

                if (match_count > 0) {
                    /* Render visible matching entities */
                    int avail_rows = rh - 1;
                    for (int row = 0; row < avail_rows &&
                         es->inspector_scroll.scroll_offset + row < match_count; row++) {
                        int idx = es->inspector_scroll.scroll_offset + row;
                        entity_node_t *ent = matches[idx];

                        bool is_cursor = (idx == es->inspector_scroll.cursor);

                        if (is_cursor && es->panel.focus == 1) {
                            wattron(rwin, A_REVERSE);
                        }

                        /* Clear the row inside border */
                        wmove(rwin, row + 2, 1);
                        for (int c = 0; c < rw; c++) waddch(rwin, ' ');

                        /* Entity name (or #<id> for anonymous) */
                        const char *display_name;
                        char id_buf[32];
                        if (ent->name && strlen(ent->name) > 0) {
                            display_name = ent->name;
                        } else {
                            snprintf(id_buf, sizeof(id_buf), "#%lu",
                                     (unsigned long)ent->id);
                            display_name = id_buf;
                        }

                        wattron(rwin, COLOR_PAIR(CP_ENTITY_NAME));
                        mvwprintw(rwin, row + 2, 2, "%.*s", rw / 2, display_name);
                        wattroff(rwin, COLOR_PAIR(CP_ENTITY_NAME));

                        /* Full path in dim to the right */
                        if (ent->full_path) {
                            int name_end = getcurx(rwin);
                            int path_col = name_end + 1;
                            int avail = rw - (path_col - 1);
                            if (avail > 2) {
                                wattron(rwin, A_DIM);
                                mvwprintw(rwin, row + 2, path_col, "%.*s",
                                          avail, ent->full_path);
                                wattroff(rwin, A_DIM);
                            }
                        }

                        if (is_cursor && es->panel.focus == 1) {
                            wattroff(rwin, A_REVERSE);
                        }
                    }
                } else {
                    const char *msg = "No entities";
                    int msg_len = (int)strlen(msg);
                    wattron(rwin, A_DIM);
                    mvwprintw(rwin, rh / 2 + 1, (rw - msg_len) / 2 + 1, "%s", msg);
                    wattroff(rwin, A_DIM);
                }

                free(matches);
            }
        } else if (comp_name && (!state->entity_list || state->entity_list->count == 0)) {
            const char *msg = "Waiting for entity data...";
            int msg_len = (int)strlen(msg);
            wattron(rwin, A_DIM);
            mvwprintw(rwin, rh / 2 + 1, (rw - msg_len) / 2 + 1, "%s", msg);
            wattroff(rwin, A_DIM);
        }
    } else if (sel && state->entity_detail) {
        /* --- Branch B: Non-component entity selected -- show entity detail --- */
        if (strcmp(state->entity_detail->path, sel->full_path) == 0) {
            const entity_detail_t *detail = state->entity_detail;

            int group_count = count_groups(detail);
            ensure_comp_expanded(es, group_count);

            int total_rows = count_inspector_rows(detail, es->comp_expanded,
                                                   es->comp_expanded_count);
            es->inspector_scroll.total_items = total_rows;
            es->inspector_scroll.visible_rows = rh;
            scroll_ensure_visible(&es->inspector_scroll);

            /* Description (from CEL_Doc) */
            int desc_rows = 0;
            if (detail->doc_brief) {
                wattron(rwin, A_DIM);
                const char *line_start = detail->doc_brief;
                int drow = 1;
                while (*line_start) {
                    const char *line_end = strchr(line_start, '\n');
                    int line_len = line_end ? (int)(line_end - line_start) : (int)strlen(line_start);
                    if (line_len > rw - 4) line_len = rw - 4;
                    if (drow < rh) {
                        mvwprintw(rwin, drow, 2, "%.*s", line_len, line_start);
                        drow++;
                        desc_rows++;
                    }
                    if (line_end) line_start = line_end + 1;
                    else break;
                }
                wattroff(rwin, A_DIM);
                if (desc_rows > 0) desc_rows++; /* blank line after description */
            }

            int logical_row = 0;
            int group_idx = 0;

            /* Render components */
            if (detail->components && yyjson_is_obj(detail->components)) {
                size_t idx, max;
                yyjson_val *key, *val;
                yyjson_obj_foreach(detail->components, idx, max, key, val) {
                    if (is_hidden_component(yyjson_get_str(key))) continue;
                    bool exp = (group_idx < es->comp_expanded_count)
                                   ? es->comp_expanded[group_idx]
                                   : true;

                    /* Count rows without rendering */
                    int rows_for_this = 1;
                    if (exp && val && !yyjson_is_null(val)) {
                        if (yyjson_is_obj(val))
                            rows_for_this += (int)yyjson_obj_size(val);
                        else if (yyjson_is_arr(val))
                            rows_for_this += (int)yyjson_arr_size(val);
                        else
                            rows_for_this += 1;
                    }

                    if (logical_row + rows_for_this > es->inspector_scroll.scroll_offset &&
                        logical_row < es->inspector_scroll.scroll_offset + rh - desc_rows) {
                        int start_render = logical_row - es->inspector_scroll.scroll_offset + 1 + desc_rows;
                        if (start_render < 1 + desc_rows) start_render = 1 + desc_rows;
                        json_render_component(rwin, yyjson_get_str(key), val,
                                              start_render, 1, rh + 1, rw, exp);
                    }
                    logical_row += rows_for_this;
                    group_idx++;
                }
            }

            /* Render Tags section */
            if (detail->tags && yyjson_is_arr(detail->tags) &&
                yyjson_arr_size(detail->tags) > 0) {
                bool tags_exp = (group_idx < es->comp_expanded_count)
                                    ? es->comp_expanded[group_idx]
                                    : true;
                int tag_rows = 1 + (tags_exp ? (int)yyjson_arr_size(detail->tags) : 0);

                if (logical_row + tag_rows > es->inspector_scroll.scroll_offset &&
                    logical_row < es->inspector_scroll.scroll_offset + rh - desc_rows) {
                    int start_render = logical_row - es->inspector_scroll.scroll_offset + 1 + desc_rows;
                    if (start_render < 1 + desc_rows) start_render = 1 + desc_rows;

                    if (start_render <= rh) {
                        wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
                        mvwprintw(rwin, start_render, 1, "%s Tags", tags_exp ? "v" : ">");
                        wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);

                        if (tags_exp) {
                            size_t tidx, tmax;
                            yyjson_val *tag;
                            int trow = start_render + 1;
                            yyjson_arr_foreach(detail->tags, tidx, tmax, tag) {
                                if (trow > rh) break;
                                if (yyjson_is_str(tag)) {
                                    wattron(rwin, COLOR_PAIR(CP_JSON_STRING));
                                    mvwprintw(rwin, trow, 3, "%s", yyjson_get_str(tag));
                                    wattroff(rwin, COLOR_PAIR(CP_JSON_STRING));
                                }
                                trow++;
                            }
                        }
                    }
                }
                logical_row += tag_rows;
                group_idx++;
            }

            /* Render Pairs section */
            if (detail->pairs && yyjson_is_obj(detail->pairs) &&
                yyjson_obj_size(detail->pairs) > 0) {
                bool pairs_exp = (group_idx < es->comp_expanded_count)
                                     ? es->comp_expanded[group_idx]
                                     : true;
                int pair_rows = 1 + (pairs_exp ? (int)yyjson_obj_size(detail->pairs) : 0);

                if (logical_row + pair_rows > es->inspector_scroll.scroll_offset &&
                    logical_row < es->inspector_scroll.scroll_offset + rh - desc_rows) {
                    int start_render = logical_row - es->inspector_scroll.scroll_offset + 1 + desc_rows;
                    if (start_render < 1 + desc_rows) start_render = 1 + desc_rows;

                    if (start_render <= rh) {
                        wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
                        mvwprintw(rwin, start_render, 1, "%s Pairs", pairs_exp ? "v" : ">");
                        wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);

                        if (pairs_exp) {
                            size_t pidx, pmax;
                            yyjson_val *pkey, *pval;
                            int prow = start_render + 1;
                            yyjson_obj_foreach(detail->pairs, pidx, pmax, pkey, pval) {
                                if (prow > rh) break;
                                wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
                                mvwprintw(rwin, prow, 3, "%s", yyjson_get_str(pkey));
                                wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
                                wprintw(rwin, ": ");
                                json_render_value(rwin, pval, prow, getcurx(rwin),
                                                  rh + 1, rw);
                                prow++;
                            }
                        }
                    }
                }
                logical_row += pair_rows;
                group_idx++;
            }

            (void)logical_row;
        } else {
            const char *msg = "Loading...";
            int msg_len = (int)strlen(msg);
            wattron(rwin, A_DIM);
            mvwprintw(rwin, rh / 2, (rw - msg_len) / 2 + 1, "%s", msg);
            wattroff(rwin, A_DIM);
        }
    } else if (!sel) {
        /* --- Branch C: Section header selected or no data --- */
        const char *msg = "Select an entity";
        int msg_len = (int)strlen(msg);
        wattron(rwin, A_DIM);
        mvwprintw(rwin, rh / 2, (rw - msg_len) / 2 + 1, "%s", msg);
        wattroff(rwin, A_DIM);
    } else {
        const char *msg = "Loading...";
        int msg_len = (int)strlen(msg);
        wattron(rwin, A_DIM);
        mvwprintw(rwin, rh / 2, (rw - msg_len) / 2 + 1, "%s", msg);
        wattroff(rwin, A_DIM);
    }

    split_panel_refresh(&es->panel);
}

/* --- Input --- */

bool tab_ecs_input(tab_t *self, int ch, void *app_state) {
    ecs_state_t *es = (ecs_state_t *)self->state;
    if (!es) return false;

    app_state_t *state = (app_state_t *)app_state;

    /* Focus switching: left/right arrows */
    if (split_panel_handle_focus(&es->panel, ch)) return true;

    /* Left panel focused: entity tree navigation */
    if (es->panel.focus == 0) {
        switch (ch) {
        case KEY_UP:
        case 'k':
            scroll_move(&es->tree.scroll, -1);
            sync_selected_path(es, state);
            return true;

        case KEY_DOWN:
        case 'j':
            scroll_move(&es->tree.scroll, +1);
            sync_selected_path(es, state);
            return true;

        case KEY_PPAGE:
            scroll_page(&es->tree.scroll, -1);
            sync_selected_path(es, state);
            return true;

        case KEY_NPAGE:
            scroll_page(&es->tree.scroll, +1);
            sync_selected_path(es, state);
            return true;

        case 'g':
            scroll_to_top(&es->tree.scroll);
            sync_selected_path(es, state);
            return true;

        case 'G':
            scroll_to_bottom(&es->tree.scroll);
            sync_selected_path(es, state);
            return true;

        case KEY_ENTER:
        case '\n':
        case '\r':
            tree_view_toggle_expand(&es->tree, state->entity_list);
            return true;

        case 'f':
            tree_view_toggle_anonymous(&es->tree, state->entity_list);
            sync_selected_path(es, state);
            return true;
        }
    }

    /* Right panel focused: context-sensitive input */
    if (es->panel.focus == 1) {
        entity_node_t *sel = tree_view_selected(&es->tree);

        if (sel && sel->entity_class == ENTITY_CLASS_SYSTEM) {
            /* System detail mode: scroll matched entities + cross-navigate */
            switch (ch) {
            case KEY_UP:
            case 'k':
                scroll_move(&es->inspector_scroll, -1);
                return true;
            case KEY_DOWN:
            case 'j':
                scroll_move(&es->inspector_scroll, +1);
                return true;
            case KEY_PPAGE:
                scroll_page(&es->inspector_scroll, -1);
                return true;
            case KEY_NPAGE:
                scroll_page(&es->inspector_scroll, +1);
                return true;
            case 'g':
                scroll_to_top(&es->inspector_scroll);
                return true;
            case 'G':
                scroll_to_bottom(&es->inspector_scroll);
                return true;
            case KEY_ENTER:
            case '\n':
            case '\r': {
                /* Cross-navigate to approximate matched entity */
                int match_count = 0;
                entity_node_t **matches = NULL;
                if (state->entity_detail && sel->full_path &&
                    strcmp(state->entity_detail->path, sel->full_path) == 0) {
                    matches = build_system_matches(state->entity_detail,
                                                    state->entity_list, &match_count);
                }
                if (matches && es->inspector_scroll.cursor >= 0 &&
                    es->inspector_scroll.cursor < match_count) {
                    entity_node_t *target = matches[es->inspector_scroll.cursor];
                    if (target->full_path) {
                        cross_navigate_to_entity(es, state, target->full_path);
                    }
                }
                free(matches);
                return true;
            }
            }
        } else if (sel && sel->entity_class == ENTITY_CLASS_COMPONENT) {
            /* Component mode: scroll through entities-with-component list */
            switch (ch) {
            case KEY_UP:
            case 'k':
                scroll_move(&es->inspector_scroll, -1);
                return true;

            case KEY_DOWN:
            case 'j':
                scroll_move(&es->inspector_scroll, +1);
                return true;

            case KEY_PPAGE:
                scroll_page(&es->inspector_scroll, -1);
                return true;

            case KEY_NPAGE:
                scroll_page(&es->inspector_scroll, +1);
                return true;

            case 'g':
                scroll_to_top(&es->inspector_scroll);
                return true;

            case 'G':
                scroll_to_bottom(&es->inspector_scroll);
                return true;
            }
        } else {
            /* Entity detail mode: scroll + expand/collapse component groups */
            switch (ch) {
            case KEY_UP:
            case 'k':
                scroll_move(&es->inspector_scroll, -1);
                return true;

            case KEY_DOWN:
            case 'j':
                scroll_move(&es->inspector_scroll, +1);
                return true;

            case KEY_PPAGE:
                scroll_page(&es->inspector_scroll, -1);
                return true;

            case KEY_NPAGE:
                scroll_page(&es->inspector_scroll, +1);
                return true;

            case KEY_ENTER:
            case '\n':
            case '\r':
                if (state->entity_detail) {
                    int gi = cursor_to_group_index(state->entity_detail,
                                                   es->comp_expanded,
                                                   es->comp_expanded_count,
                                                   es->inspector_scroll.cursor);
                    if (gi >= 0 && gi < es->comp_expanded_count) {
                        es->comp_expanded[gi] = !es->comp_expanded[gi];
                    }
                }
                return true;
            }
        }
    }

    return false;
}
