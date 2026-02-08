#define _POSIX_C_SOURCE 200809L

#include "tab_systems.h"
#include "../tui.h"
#include "../split_panel.h"
#include "../scroll.h"
#include "../data_model.h"
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Canonical Flecs pipeline phase execution order.
 * Keep in sync with tree_view.c phase_color_pair(). */
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

/* --- Display entry: flat list row (phase header or system entity) --- */

typedef struct {
    bool is_header;           /* true = phase header row */
    const char *phase_name;   /* phase name for headers */
    int phase_color;          /* color pair for phase */
    int system_count;         /* systems in phase (headers only) */
    entity_node_t *entity;    /* entity pointer (system rows only) */
} display_entry_t;

/* Per-tab private state */
typedef struct systems_state {
    split_panel_t panel;              /* Left/right split */
    scroll_state_t left_scroll;       /* Scroll state for left panel list */
    scroll_state_t inspector_scroll;  /* Scroll state for inspector content */
    bool panel_created;               /* Whether split_panel windows exist */

    /* Flat display list */
    display_entry_t *entries;
    int entry_count;
    int entry_capacity;
} systems_state_t;

/* --- Classification helpers (copied from tab_cels.c for system detection) --- */

static bool has_tag(entity_node_t *node, const char *tag_name) {
    for (int i = 0; i < node->tag_count; i++) {
        if (node->tags[i] && strstr(node->tags[i], tag_name)) return true;
    }
    return false;
}

static char *extract_pipeline_phase(entity_node_t *node) {
    for (int i = 0; i < node->tag_count; i++) {
        if (node->tags[i] && strncmp(node->tags[i], "flecs.pipeline.", 15) == 0) {
            const char *phase = node->tags[i] + 15;
            if (phase[0] != '\0') return strdup(phase);
        }
    }
    return NULL;
}

/* Classify system entities: set entity_class and class_detail for systems.
 * Only touches system entities -- leaves others unchanged. */
static void classify_systems(entity_list_t *list) {
    if (!list) return;
    for (int i = 0; i < list->root_count; i++) {
        entity_node_t *node = list->roots[i];
        if (has_tag(node, "flecs.system.System")) {
            node->entity_class = ENTITY_CLASS_SYSTEM;
            free(node->class_detail);
            node->class_detail = extract_pipeline_phase(node);
            if (!node->class_detail) node->class_detail = strdup("System");
        } else if (has_tag(node, "flecs.core.Observer")) {
            node->entity_class = ENTITY_CLASS_SYSTEM;
            free(node->class_detail);
            node->class_detail = strdup("Observer");
        }
    }
}

/* --- Enrichment: merge pipeline stats into system entity nodes --- */

static void enrich_systems_with_pipeline(entity_list_t *list,
                                          system_registry_t *reg) {
    if (!list || !reg || reg->count == 0) return;

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

/* --- Build flat display list --- */

static void rebuild_display_list(systems_state_t *ss, entity_list_t *list) {
    ss->entry_count = 0;
    if (!list) return;

    /* Collect unique phases from system entities in canonical order */
    struct { const char *name; int count; } phases[32];
    int phase_count = 0;

    /* First pass: count systems per phase */
    for (int p = 0; p < PHASE_ORDER_COUNT; p++) {
        int count = 0;
        for (int i = 0; i < list->root_count; i++) {
            entity_node_t *node = list->roots[i];
            if (node->entity_class != ENTITY_CLASS_SYSTEM) continue;
            if (!node->class_detail) continue;
            if (strcmp(node->class_detail, PHASE_ORDER[p]) == 0) count++;
        }
        if (count > 0 && phase_count < 32) {
            phases[phase_count].name = PHASE_ORDER[p];
            phases[phase_count].count = count;
            phase_count++;
        }
    }

    /* Also collect "Custom" phase (unknown phases not in PHASE_ORDER) */
    int custom_count = 0;
    for (int i = 0; i < list->root_count; i++) {
        entity_node_t *node = list->roots[i];
        if (node->entity_class != ENTITY_CLASS_SYSTEM) continue;
        if (!node->class_detail) { custom_count++; continue; }
        bool known = false;
        for (int p = 0; p < PHASE_ORDER_COUNT; p++) {
            if (strcmp(node->class_detail, PHASE_ORDER[p]) == 0) { known = true; break; }
        }
        /* Observer is a class_detail value for observers */
        if (!known && strcmp(node->class_detail, "Observer") == 0) {
            /* Count observers separately */
        } else if (!known && strcmp(node->class_detail, "System") == 0) {
            custom_count++;
        } else if (!known) {
            custom_count++;
        }
    }

    /* Count observers */
    int observer_count = 0;
    for (int i = 0; i < list->root_count; i++) {
        entity_node_t *node = list->roots[i];
        if (node->entity_class != ENTITY_CLASS_SYSTEM) continue;
        if (node->class_detail && strcmp(node->class_detail, "Observer") == 0) {
            observer_count++;
        }
    }

    /* Calculate total entries needed */
    int total = 0;
    for (int p = 0; p < phase_count; p++) {
        total += 1 + phases[p].count; /* header + systems */
    }
    if (observer_count > 0) total += 1 + observer_count;
    if (custom_count > 0) total += 1 + custom_count;

    /* Ensure capacity */
    if (total > ss->entry_capacity) {
        display_entry_t *new_entries = realloc(ss->entries,
            (size_t)total * sizeof(display_entry_t));
        if (!new_entries) return;
        ss->entries = new_entries;
        ss->entry_capacity = total;
    }

    int idx = 0;

    /* Emit entries for each canonical phase */
    for (int p = 0; p < phase_count; p++) {
        /* Phase header */
        ss->entries[idx].is_header = true;
        ss->entries[idx].phase_name = phases[p].name;
        ss->entries[idx].phase_color = phase_color_pair(phases[p].name);
        ss->entries[idx].system_count = phases[p].count;
        ss->entries[idx].entity = NULL;
        idx++;

        /* System entities in this phase */
        for (int i = 0; i < list->root_count; i++) {
            entity_node_t *node = list->roots[i];
            if (node->entity_class != ENTITY_CLASS_SYSTEM) continue;
            if (!node->class_detail) continue;
            if (strcmp(node->class_detail, phases[p].name) != 0) continue;

            ss->entries[idx].is_header = false;
            ss->entries[idx].phase_name = phases[p].name;
            ss->entries[idx].phase_color = phase_color_pair(phases[p].name);
            ss->entries[idx].system_count = 0;
            ss->entries[idx].entity = node;
            idx++;
        }
    }

    /* Emit Observers group */
    if (observer_count > 0) {
        ss->entries[idx].is_header = true;
        ss->entries[idx].phase_name = "Observer";
        ss->entries[idx].phase_color = CP_PHASE_CUSTOM;
        ss->entries[idx].system_count = observer_count;
        ss->entries[idx].entity = NULL;
        idx++;

        for (int i = 0; i < list->root_count; i++) {
            entity_node_t *node = list->roots[i];
            if (node->entity_class != ENTITY_CLASS_SYSTEM) continue;
            if (!node->class_detail || strcmp(node->class_detail, "Observer") != 0) continue;

            ss->entries[idx].is_header = false;
            ss->entries[idx].phase_name = "Observer";
            ss->entries[idx].phase_color = CP_PHASE_CUSTOM;
            ss->entries[idx].system_count = 0;
            ss->entries[idx].entity = node;
            idx++;
        }
    }

    /* Emit Custom group (unknown phases) */
    if (custom_count > 0) {
        ss->entries[idx].is_header = true;
        ss->entries[idx].phase_name = "Custom";
        ss->entries[idx].phase_color = CP_PHASE_CUSTOM;
        ss->entries[idx].system_count = custom_count;
        ss->entries[idx].entity = NULL;
        idx++;

        for (int i = 0; i < list->root_count; i++) {
            entity_node_t *node = list->roots[i];
            if (node->entity_class != ENTITY_CLASS_SYSTEM) continue;

            /* Skip known phases and observers */
            bool skip = false;
            if (node->class_detail) {
                if (strcmp(node->class_detail, "Observer") == 0) skip = true;
                for (int p = 0; p < PHASE_ORDER_COUNT && !skip; p++) {
                    if (strcmp(node->class_detail, PHASE_ORDER[p]) == 0) skip = true;
                }
            }
            if (skip) continue;

            ss->entries[idx].is_header = false;
            ss->entries[idx].phase_name = "Custom";
            ss->entries[idx].phase_color = CP_PHASE_CUSTOM;
            ss->entries[idx].system_count = 0;
            ss->entries[idx].entity = node;
            idx++;
        }
    }

    ss->entry_count = idx;
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
                                systems_state_t *ss) {
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

        wattron(rwin, A_DIM);
        mvwprintw(rwin, match_header_row, 19, "(approx)");
        wattroff(rwin, A_DIM);

        int match_count = 0;
        entity_node_t **matches = NULL;
        if (state->entity_detail && sel->full_path &&
            strcmp(state->entity_detail->path, sel->full_path) == 0) {
            matches = build_system_matches(state->entity_detail,
                                            state->entity_list, &match_count);
        }

        int avail_rows = rh - (match_header_row + 1);
        if (avail_rows < 1) avail_rows = 1;

        ss->inspector_scroll.total_items = match_count;
        ss->inspector_scroll.visible_rows = avail_rows;
        scroll_ensure_visible(&ss->inspector_scroll);

        if (match_count > 0 && matches) {
            for (int r = 0; r < avail_rows; r++) {
                int mi = ss->inspector_scroll.scroll_offset + r;
                if (mi >= match_count) break;

                int disp_row = match_header_row + 1 + r;
                entity_node_t *ent = matches[mi];
                bool is_cursor = (mi == ss->inspector_scroll.cursor &&
                                  ss->panel.focus == 1);

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

/* --- Inspector: pipeline visualization (phase header selected) --- */

static void draw_pipeline_viz(WINDOW *rwin, int rh, int rw,
                               systems_state_t *ss,
                               const char *selected_phase,
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

    /* Collect unique phase headers from the display list */
    const char *phases[32];
    int pcounts[32];
    int pcount = 0;

    for (int i = 0; i < ss->entry_count; i++) {
        if (ss->entries[i].is_header && pcount < 32) {
            phases[pcount] = ss->entries[i].phase_name;
            pcounts[pcount] = ss->entries[i].system_count;
            pcount++;
        }
    }

    for (int p = 0; p < pcount; p++) {
        if (row >= rh) break;

        bool is_selected = (selected_phase &&
                            strcmp(phases[p], selected_phase) == 0);
        int cp = phase_color_pair(phases[p]);

        if (is_selected) wattron(rwin, A_REVERSE);

        /* Phase name in color */
        wattron(rwin, COLOR_PAIR(cp) | A_BOLD);
        mvwprintw(rwin, row, 3, "%-14s", phases[p]);
        wattroff(rwin, COLOR_PAIR(cp) | A_BOLD);

        /* System count */
        int sys_count = pcounts[p];
        wprintw(rwin, " %d system%s", sys_count, sys_count == 1 ? "" : "s");
        total_systems += sys_count;

        /* Timing: sum time_spent_ms for systems in this phase */
        if (state->system_registry && state->entity_list) {
            double phase_time = 0.0;
            for (int s = 0; s < state->system_registry->count; s++) {
                system_info_t *si = &state->system_registry->systems[s];
                for (int e = 0; e < state->entity_list->root_count; e++) {
                    entity_node_t *en = state->entity_list->roots[e];
                    if (en->entity_class == ENTITY_CLASS_SYSTEM &&
                        en->name && si->name &&
                        strcmp(en->name, si->name) == 0 &&
                        en->class_detail &&
                        strcmp(en->class_detail, phases[p]) == 0) {
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
        if (p < pcount - 1 && row + 1 < rh) {
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

/* --- Inspector: systems summary (top-level, no specific selection) --- */

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

    char *dist_phases[32];
    int dist_counts[32];
    int dist_count = 0;

    for (int i = 0; i < state->entity_list->root_count; i++) {
        entity_node_t *n = state->entity_list->roots[i];
        if (n->entity_class != ENTITY_CLASS_SYSTEM) continue;
        const char *ph = n->class_detail ? n->class_detail : "Unknown";
        bool found = false;
        for (int p = 0; p < dist_count; p++) {
            if (strcmp(dist_phases[p], ph) == 0) { dist_counts[p]++; found = true; break; }
        }
        if (!found && dist_count < 32) {
            dist_phases[dist_count] = (char *)ph;
            dist_counts[dist_count] = 1;
            dist_count++;
        }
    }

    for (int p = 0; p < dist_count && row < rh; p++) {
        int cp = phase_color_pair(dist_phases[p]);
        wattron(rwin, COLOR_PAIR(cp));
        mvwprintw(rwin, row, 3, "%-14s", dist_phases[p]);
        wattroff(rwin, COLOR_PAIR(cp));
        wprintw(rwin, " %d", dist_counts[p]);
        row++;
    }
}

/* --- Lifecycle --- */

void tab_systems_init(tab_t *self) {
    systems_state_t *ss = calloc(1, sizeof(systems_state_t));
    if (!ss) return;

    scroll_reset(&ss->left_scroll);
    scroll_reset(&ss->inspector_scroll);
    ss->panel_created = false;
    ss->entries = NULL;
    ss->entry_count = 0;
    ss->entry_capacity = 0;

    self->state = ss;
}

void tab_systems_fini(tab_t *self) {
    systems_state_t *ss = (systems_state_t *)self->state;
    if (!ss) return;

    if (ss->panel_created) {
        split_panel_destroy(&ss->panel);
    }
    free(ss->entries);
    free(ss);
    self->state = NULL;
}

/* --- Draw --- */

void tab_systems_draw(const tab_t *self, WINDOW *win, const void *app_state) {
    systems_state_t *ss = (systems_state_t *)self->state;
    if (!ss) return;

    const app_state_t *state = (const app_state_t *)app_state;

    int h = getmaxy(win);
    int w = getmaxx(win);

    if (!ss->panel_created) {
        split_panel_create(&ss->panel, h, w, getbegy(win));
        ss->panel_created = true;
    } else if (h != ss->panel.height ||
               w != ss->panel.left_width + ss->panel.right_width) {
        split_panel_resize(&ss->panel, h, w, getbegy(win));
    }

    werase(ss->panel.left);
    werase(ss->panel.right);
    split_panel_draw_borders(&ss->panel, "Systems", "Inspector");

    /* --- Left panel: system list --- */
    if (state->entity_list) {
        /* Classify system entities */
        classify_systems(state->entity_list);

        /* Enrich with pipeline stats (match count, disabled state) */
        enrich_systems_with_pipeline(state->entity_list, state->system_registry);

        /* Build flat display list */
        rebuild_display_list(ss, state->entity_list);

        /* Update scroll state */
        int lh = getmaxy(ss->panel.left) - 2; /* minus border rows */
        int lw = getmaxx(ss->panel.left) - 2;
        ss->left_scroll.total_items = ss->entry_count;
        ss->left_scroll.visible_rows = lh;
        scroll_ensure_visible(&ss->left_scroll);

        /* Auto-select first system entity for detail polling */
        if (!state->selected_entity_path && ss->entry_count > 0) {
            for (int i = 0; i < ss->entry_count; i++) {
                if (!ss->entries[i].is_header && ss->entries[i].entity &&
                    ss->entries[i].entity->full_path) {
                    app_state_t *mut_state = (app_state_t *)state;
                    mut_state->selected_entity_path =
                        strdup(ss->entries[i].entity->full_path);
                    ss->left_scroll.cursor = i;
                    break;
                }
            }
        }

        /* Render visible entries */
        for (int r = 0; r < lh; r++) {
            int idx = ss->left_scroll.scroll_offset + r;
            if (idx >= ss->entry_count) break;

            display_entry_t *entry = &ss->entries[idx];
            bool is_cursor = (idx == ss->left_scroll.cursor &&
                              ss->panel.focus == 0);
            int draw_row = r + 1; /* +1 for top border */

            if (is_cursor) wattron(ss->panel.left, A_REVERSE);

            /* Clear row */
            wmove(ss->panel.left, draw_row, 1);
            for (int c = 0; c < lw; c++) waddch(ss->panel.left, ' ');

            if (entry->is_header) {
                /* Phase header: bold, colored, with count */
                wattron(ss->panel.left, COLOR_PAIR(entry->phase_color) | A_BOLD);
                mvwprintw(ss->panel.left, draw_row, 2, "%s", entry->phase_name);
                wattroff(ss->panel.left, COLOR_PAIR(entry->phase_color) | A_BOLD);

                /* System count */
                wattron(ss->panel.left, A_DIM);
                wprintw(ss->panel.left, " (%d)", entry->system_count);
                wattroff(ss->panel.left, A_DIM);
            } else {
                /* System entity row: indented with timing info */
                entity_node_t *node = entry->entity;
                const char *name = node->name ? node->name : "(unnamed)";

                /* Name */
                if (node->disabled) {
                    wattron(ss->panel.left, COLOR_PAIR(CP_SYSTEM_DISABLED));
                    mvwprintw(ss->panel.left, draw_row, 4, "%.*s",
                              lw / 2, name);
                    wattroff(ss->panel.left, COLOR_PAIR(CP_SYSTEM_DISABLED));
                } else {
                    wattron(ss->panel.left, COLOR_PAIR(CP_ENTITY_NAME));
                    mvwprintw(ss->panel.left, draw_row, 4, "%.*s",
                              lw / 2, name);
                    wattroff(ss->panel.left, COLOR_PAIR(CP_ENTITY_NAME));
                }

                /* Timing info on the right */
                system_info_t *sinfo = find_system_info(state, node->name);
                if (sinfo && sinfo->time_spent_ms > 0.0) {
                    char timing[32];
                    snprintf(timing, sizeof(timing), "%.2fms", sinfo->time_spent_ms);
                    int tlen = (int)strlen(timing);
                    int tcol = lw - tlen;
                    if (tcol > lw / 2) {
                        wattron(ss->panel.left, COLOR_PAIR(CP_JSON_NUMBER));
                        mvwprintw(ss->panel.left, draw_row, tcol, "%s", timing);
                        wattroff(ss->panel.left, COLOR_PAIR(CP_JSON_NUMBER));
                    }
                }

                /* Match count next to timing */
                if (node->system_match_count > 0) {
                    int name_end = getcurx(ss->panel.left);
                    if (name_end < lw / 2 + 5) name_end = lw / 2 + 5;
                    if (name_end < lw - 15) {
                        wattron(ss->panel.left, A_DIM);
                        mvwprintw(ss->panel.left, draw_row, name_end + 1,
                                  "%d matched", node->system_match_count);
                        wattroff(ss->panel.left, A_DIM);
                    }
                }
            }

            if (is_cursor) wattroff(ss->panel.left, A_REVERSE);
        }
    } else {
        const char *msg = "Waiting for data...";
        int msg_len = (int)strlen(msg);
        int max_y = getmaxy(ss->panel.left);
        int max_x = getmaxx(ss->panel.left);
        wattron(ss->panel.left, A_DIM);
        mvwprintw(ss->panel.left, max_y / 2, (max_x - msg_len) / 2, "%s", msg);
        wattroff(ss->panel.left, A_DIM);
    }

    /* --- Right panel: context-sensitive inspector --- */
    WINDOW *rwin = ss->panel.right;
    int rh = getmaxy(rwin) - 2;
    int rw = getmaxx(rwin) - 2;

    if (ss->entry_count > 0 && ss->left_scroll.cursor >= 0 &&
        ss->left_scroll.cursor < ss->entry_count) {
        display_entry_t *cur = &ss->entries[ss->left_scroll.cursor];

        if (cur->is_header) {
            /* Phase header selected -> pipeline visualization */
            draw_pipeline_viz(rwin, rh, rw, ss, cur->phase_name, state);
        } else if (cur->entity) {
            /* System entity selected -> system detail */
            draw_system_detail(rwin, rh, rw, cur->entity, state, ss);
        }
    } else {
        /* No selection */
        draw_systems_summary(rwin, rh, rw, state);
    }

    split_panel_refresh(&ss->panel);
}

/* --- Input --- */

bool tab_systems_input(tab_t *self, int ch, void *app_state) {
    systems_state_t *ss = (systems_state_t *)self->state;
    if (!ss) return false;

    app_state_t *state = (app_state_t *)app_state;

    /* Focus switching: left/right arrows */
    if (split_panel_handle_focus(&ss->panel, ch)) return true;

    /* Left panel focused: system list navigation */
    if (ss->panel.focus == 0) {
        switch (ch) {
        case KEY_UP:
        case 'k':
            scroll_move(&ss->left_scroll, -1);
            /* Update selected_entity_path for detail polling */
            if (ss->left_scroll.cursor >= 0 &&
                ss->left_scroll.cursor < ss->entry_count) {
                display_entry_t *cur = &ss->entries[ss->left_scroll.cursor];
                if (!cur->is_header && cur->entity && cur->entity->full_path) {
                    free(state->selected_entity_path);
                    state->selected_entity_path = strdup(cur->entity->full_path);
                    /* Invalidate stale detail */
                    if (state->entity_detail &&
                        strcmp(state->entity_detail->path, cur->entity->full_path) != 0) {
                        entity_detail_free(state->entity_detail);
                        state->entity_detail = NULL;
                    }
                }
            }
            return true;

        case KEY_DOWN:
        case 'j':
            scroll_move(&ss->left_scroll, +1);
            if (ss->left_scroll.cursor >= 0 &&
                ss->left_scroll.cursor < ss->entry_count) {
                display_entry_t *cur = &ss->entries[ss->left_scroll.cursor];
                if (!cur->is_header && cur->entity && cur->entity->full_path) {
                    free(state->selected_entity_path);
                    state->selected_entity_path = strdup(cur->entity->full_path);
                    if (state->entity_detail &&
                        strcmp(state->entity_detail->path, cur->entity->full_path) != 0) {
                        entity_detail_free(state->entity_detail);
                        state->entity_detail = NULL;
                    }
                }
            }
            return true;

        case KEY_PPAGE:
            scroll_page(&ss->left_scroll, -1);
            return true;

        case KEY_NPAGE:
            scroll_page(&ss->left_scroll, +1);
            return true;

        case 'g':
            scroll_to_top(&ss->left_scroll);
            return true;

        case 'G':
            scroll_to_bottom(&ss->left_scroll);
            return true;

        case KEY_ENTER:
        case '\n':
        case '\r':
            /* On system entity: cross-navigate to CELS tab */
            if (ss->left_scroll.cursor >= 0 &&
                ss->left_scroll.cursor < ss->entry_count) {
                display_entry_t *cur = &ss->entries[ss->left_scroll.cursor];
                if (!cur->is_header && cur->entity && cur->entity->full_path) {
                    /* Request cross-navigation to CELS tab (index 1) */
                    state->pending_tab = 1;
                    free(state->selected_entity_path);
                    state->selected_entity_path = strdup(cur->entity->full_path);
                    return true;
                }
            }
            return true;
        }
    }

    /* Right panel focused: inspector navigation */
    if (ss->panel.focus == 1) {
        switch (ch) {
        case KEY_UP:
        case 'k':
            scroll_move(&ss->inspector_scroll, -1);
            return true;
        case KEY_DOWN:
        case 'j':
            scroll_move(&ss->inspector_scroll, +1);
            return true;
        case KEY_PPAGE:
            scroll_page(&ss->inspector_scroll, -1);
            return true;
        case KEY_NPAGE:
            scroll_page(&ss->inspector_scroll, +1);
            return true;
        case 'g':
            scroll_to_top(&ss->inspector_scroll);
            return true;
        case 'G':
            scroll_to_bottom(&ss->inspector_scroll);
            return true;
        }
    }

    return false;
}
