#define _POSIX_C_SOURCE 200809L
#include "tree_view.h"
#include "tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* UTF-8 box drawing characters */
#define TREE_VERT    "\xe2\x94\x82"   /* U+2502 vertical line */
#define TREE_BRANCH  "\xe2\x94\x9c"   /* U+251C branch (has sibling below) */
#define TREE_LAST    "\xe2\x94\x94"   /* U+2514 last child corner */
#define TREE_HORIZ   "\xe2\x94\x80"   /* U+2500 horizontal line */

/* Section names matching CELS-C paradigm */
static const char *section_names[ENTITY_CLASS_COUNT] = {
    [ENTITY_CLASS_COMPOSITION] = "Compositions",
    [ENTITY_CLASS_ENTITY]      = "Entities",
    [ENTITY_CLASS_LIFECYCLE]   = "Lifecycles",
    [ENTITY_CLASS_SYSTEM]      = "Systems",
    [ENTITY_CLASS_COMPONENT]   = "Components",
};

/* --- Helpers --- */

static bool node_is_last_child(entity_node_t *node) {
    if (!node->parent) return true;
    return node->parent->children[node->parent->child_count - 1] == node;
}

static bool ancestor_has_next_sibling(entity_node_t *node, int target_depth) {
    entity_node_t *ancestor = node;
    while (ancestor && ancestor->depth > target_depth) {
        ancestor = ancestor->parent;
    }
    if (!ancestor) return false;
    return !node_is_last_child(ancestor);
}

/* Add a section header row to the display list */
static void add_header_row(tree_view_t *tv, int section_idx) {
    tv->rows[tv->row_count].node = NULL;
    tv->rows[tv->row_count].section_idx = section_idx;
    tv->rows[tv->row_count].phase_group = -1;
    tv->row_count++;
}

/* DFS traversal: add entity rows to the display list */
static void dfs_collect(tree_view_t *tv, entity_node_t *node, int section_idx) {
    if (node->is_anonymous && !tv->show_anonymous) return;

    tv->rows[tv->row_count].node = node;
    tv->rows[tv->row_count].section_idx = section_idx;
    tv->rows[tv->row_count].phase_group = -1;
    tv->row_count++;

    if (node->expanded) {
        for (int i = 0; i < node->child_count; i++) {
            dfs_collect(tv, node->children[i], section_idx);
        }
    }
}

/* --- Public API --- */

void tree_view_init(tree_view_t *tv) {
    tv->rows = NULL;
    tv->row_count = 0;
    scroll_reset(&tv->scroll);
    tv->show_anonymous = false;
    tv->prev_selected_id = 0;

    /* All sections start collapsed */
    for (int i = 0; i < ENTITY_CLASS_COUNT; i++) {
        tv->section_collapsed[i] = true;
        tv->section_item_count[i] = 0;
    }

    /* Phase sub-header state */
    tv->phase_names = NULL;
    tv->phase_system_counts = NULL;
    tv->phase_collapsed = NULL;
    tv->phase_count = 0;
}

void tree_view_fini(tree_view_t *tv) {
    free(tv->rows);
    tv->rows = NULL;
    tv->row_count = 0;
    scroll_reset(&tv->scroll);
    tv->prev_selected_id = 0;

    /* Free phase data */
    for (int i = 0; i < tv->phase_count; i++) {
        free(tv->phase_names[i]);
    }
    free(tv->phase_names);
    free(tv->phase_system_counts);
    free(tv->phase_collapsed);
    tv->phase_names = NULL;
    tv->phase_system_counts = NULL;
    tv->phase_collapsed = NULL;
    tv->phase_count = 0;
}

void tree_view_set_phases(tree_view_t *tv, char **phase_names,
                          int *phase_system_counts, int phase_count) {
    /* Save old state for collapse preservation */
    char **old_names = tv->phase_names;
    int *old_counts = tv->phase_system_counts;
    bool *old_collapsed = tv->phase_collapsed;
    int old_count = tv->phase_count;

    if (phase_count == 0) {
        /* Free old data and clear */
        for (int i = 0; i < old_count; i++) free(old_names[i]);
        free(old_names);
        free(old_counts);
        free(old_collapsed);
        tv->phase_names = NULL;
        tv->phase_system_counts = NULL;
        tv->phase_collapsed = NULL;
        tv->phase_count = 0;
        return;
    }

    /* Allocate new arrays */
    tv->phase_names = malloc(sizeof(char *) * (size_t)phase_count);
    tv->phase_system_counts = malloc(sizeof(int) * (size_t)phase_count);
    tv->phase_collapsed = malloc(sizeof(bool) * (size_t)phase_count);
    tv->phase_count = phase_count;

    for (int i = 0; i < phase_count; i++) {
        tv->phase_names[i] = strdup(phase_names[i]);
        tv->phase_system_counts[i] = phase_system_counts[i];

        /* Preserve collapse state: search old names for match */
        tv->phase_collapsed[i] = false; /* default: expanded */
        for (int j = 0; j < old_count; j++) {
            if (old_names && old_names[j] &&
                strcmp(old_names[j], phase_names[i]) == 0) {
                tv->phase_collapsed[i] = old_collapsed[j];
                break;
            }
        }
    }

    /* Free old data */
    for (int i = 0; i < old_count; i++) free(old_names[i]);
    free(old_names);
    free(old_counts);
    free(old_collapsed);
}

void tree_view_rebuild_visible(tree_view_t *tv, entity_list_t *list) {
    if (!list) {
        free(tv->rows);
        tv->rows = NULL;
        tv->row_count = 0;
        tv->scroll.total_items = 0;
        memset(tv->section_item_count, 0, sizeof(tv->section_item_count));
        scroll_ensure_visible(&tv->scroll);
        return;
    }

    /* Remember which entity was selected */
    uint64_t prev_id = 0;
    if (tv->rows && tv->row_count > 0 &&
        tv->scroll.cursor >= 0 && tv->scroll.cursor < tv->row_count) {
        display_row_t *cur = &tv->rows[tv->scroll.cursor];
        if (cur->node) prev_id = cur->node->id;
    }
    tv->prev_selected_id = prev_id;

    free(tv->rows);

    /* First pass: count entities per section (for header display) */
    memset(tv->section_item_count, 0, sizeof(tv->section_item_count));
    for (int i = 0; i < list->root_count; i++) {
        int cls = (int)list->roots[i]->entity_class;
        if (cls >= 0 && cls < ENTITY_CLASS_COUNT) {
            tv->section_item_count[cls]++;
        }
    }

    /* Max possible rows: headers + phase sub-headers + all entities */
    int max_rows = ENTITY_CLASS_COUNT + tv->phase_count + 1 + list->count;
    tv->rows = calloc((size_t)max_rows, sizeof(display_row_t));
    tv->row_count = 0;
    if (!tv->rows) return;

    /* Build display list: for each section with items, add header + items */
    for (int cls = 0; cls < ENTITY_CLASS_COUNT; cls++) {
        if (tv->section_item_count[cls] == 0) continue;

        /* Section header (always visible) */
        add_header_row(tv, cls);

        /* Items only if section is expanded */
        if (!tv->section_collapsed[cls]) {
            if (cls == ENTITY_CLASS_SYSTEM && tv->phase_count > 0) {
                /* Systems section: group under phase sub-headers */
                bool *collected = calloc((size_t)list->root_count, sizeof(bool));

                for (int p = 0; p < tv->phase_count; p++) {
                    if (tv->phase_system_counts[p] == 0) continue;

                    /* Phase sub-header row */
                    tv->rows[tv->row_count].node = NULL;
                    tv->rows[tv->row_count].section_idx = ENTITY_CLASS_SYSTEM;
                    tv->rows[tv->row_count].phase_group = p;
                    tv->row_count++;

                    /* Systems in this phase (only if phase not collapsed) */
                    if (!tv->phase_collapsed[p]) {
                        for (int i = 0; i < list->root_count; i++) {
                            entity_node_t *root = list->roots[i];
                            if ((int)root->entity_class != ENTITY_CLASS_SYSTEM) continue;
                            if (!root->class_detail) continue;
                            if (strcmp(root->class_detail, tv->phase_names[p]) != 0) continue;
                            collected[i] = true;
                            dfs_collect(tv, root, cls);
                        }
                    } else {
                        /* Mark as collected even if collapsed */
                        for (int i = 0; i < list->root_count; i++) {
                            entity_node_t *root = list->roots[i];
                            if ((int)root->entity_class != ENTITY_CLASS_SYSTEM) continue;
                            if (!root->class_detail) continue;
                            if (strcmp(root->class_detail, tv->phase_names[p]) == 0) {
                                collected[i] = true;
                            }
                        }
                    }
                }

                /* Remaining systems not matching any known phase ("Custom") */
                bool has_custom = false;
                for (int i = 0; i < list->root_count; i++) {
                    if ((int)list->roots[i]->entity_class != ENTITY_CLASS_SYSTEM) continue;
                    if (!collected[i]) { has_custom = true; break; }
                }
                if (has_custom) {
                    /* Add "Custom" phase sub-header at index -2 sentinel
                     * -- use phase_count as the index for custom group */
                    tv->rows[tv->row_count].node = NULL;
                    tv->rows[tv->row_count].section_idx = ENTITY_CLASS_SYSTEM;
                    tv->rows[tv->row_count].phase_group = tv->phase_count; /* custom sentinel */
                    tv->row_count++;

                    for (int i = 0; i < list->root_count; i++) {
                        if ((int)list->roots[i]->entity_class != ENTITY_CLASS_SYSTEM) continue;
                        if (!collected[i]) {
                            dfs_collect(tv, list->roots[i], cls);
                        }
                    }
                }

                free(collected);
            } else {
                /* Non-system sections: unchanged behavior */
                for (int i = 0; i < list->root_count; i++) {
                    if ((int)list->roots[i]->entity_class == cls) {
                        dfs_collect(tv, list->roots[i], cls);
                    }
                }
            }
        }
    }

    /* Update scroll total */
    tv->scroll.total_items = tv->row_count;

    /* Preserve cursor: find same entity by id */
    if (prev_id != 0) {
        bool found = false;
        for (int i = 0; i < tv->row_count; i++) {
            if (tv->rows[i].node && tv->rows[i].node->id == prev_id) {
                tv->scroll.cursor = i;
                found = true;
                break;
            }
        }
        if (!found && tv->scroll.cursor >= tv->row_count) {
            tv->scroll.cursor = tv->row_count > 0 ? tv->row_count - 1 : 0;
        }
    }

    scroll_ensure_visible(&tv->scroll);
}

void tree_view_toggle_expand(tree_view_t *tv, entity_list_t *list) {
    if (!tv->rows || tv->row_count == 0) return;
    if (tv->scroll.cursor < 0 || tv->scroll.cursor >= tv->row_count) return;

    display_row_t *cur = &tv->rows[tv->scroll.cursor];

    if (!cur->node) {
        if (cur->phase_group >= 0 && cur->phase_group < tv->phase_count) {
            /* Phase sub-header: toggle phase collapse */
            tv->phase_collapsed[cur->phase_group] = !tv->phase_collapsed[cur->phase_group];
        } else if (cur->phase_group == -1) {
            /* Section header: toggle section collapse */
            int s = cur->section_idx;
            if (s >= 0 && s < ENTITY_CLASS_COUNT) {
                tv->section_collapsed[s] = !tv->section_collapsed[s];
            }
        }
        /* phase_group == phase_count is the "Custom" group -- not collapsible */
    } else if (cur->node->child_count > 0) {
        /* Entity with children: toggle tree expand */
        cur->node->expanded = !cur->node->expanded;
    }

    tree_view_rebuild_visible(tv, list);
}

void tree_view_toggle_anonymous(tree_view_t *tv, entity_list_t *list) {
    tv->show_anonymous = !tv->show_anonymous;
    tree_view_rebuild_visible(tv, list);
}

entity_node_t *tree_view_selected(tree_view_t *tv) {
    if (!tv->rows || tv->row_count == 0) return NULL;
    if (tv->scroll.cursor < 0 || tv->scroll.cursor >= tv->row_count) return NULL;
    return tv->rows[tv->scroll.cursor].node;  /* NULL if on a header */
}

/* Draw a section header: bold first letter + rest, with collapse indicator.
 * First letters spell C-E-L-S-C vertically (the CELS paradigm). */
static void draw_section_header(WINDOW *win, int row, int max_cols,
                                const char *name, int count,
                                bool collapsed, bool is_cursor) {
    if (is_cursor) {
        wattron(win, A_REVERSE);
        wmove(win, row, 1);
        for (int c = 0; c < max_cols; c++) waddch(win, ' ');
    }

    /* Collapse indicator */
    mvwprintw(win, row, 1, "%s ", collapsed ? ">" : "v");

    /* Bold first letter (spells CELS-C vertically) */
    wattron(win, A_BOLD | COLOR_PAIR(CP_LABEL));
    waddch(win, (chtype)name[0]);
    wattroff(win, A_BOLD);

    /* Rest of name + count (normal weight, same color) */
    wprintw(win, "%s (%d)", name + 1, count);
    wattroff(win, COLOR_PAIR(CP_LABEL));

    /* Fill rest with dim horizontal line */
    int cur_x = getcurx(win);
    wattron(win, A_DIM);
    for (int x = cur_x + 1; x <= max_cols; x++) waddch(win, ACS_HLINE);
    wattroff(win, A_DIM);

    if (is_cursor) {
        wattroff(win, A_REVERSE);
    }
}

/* Canonical phase-to-color mapping for system phase tags */
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
    if (strcmp(phase, "OnStart") == 0)     return CP_PHASE_ONLOAD; /* reuse blue */
    return CP_PHASE_CUSTOM;
}

/* Draw a phase sub-header row (indented under Systems section header) */
static void draw_phase_subheader(WINDOW *win, int row, int max_cols,
                                  const char *phase_name, int sys_count,
                                  bool collapsed, bool is_cursor, int color_pair) {
    if (is_cursor) {
        wattron(win, A_REVERSE);
        wmove(win, row, 1);
        for (int c = 0; c < max_cols; c++) waddch(win, ' ');
    }

    /* Indented under section header */
    mvwprintw(win, row, 3, "%s ", collapsed ? ">" : "v");

    /* Phase name in color */
    wattron(win, COLOR_PAIR(color_pair) | A_BOLD);
    wprintw(win, "%s", phase_name);
    wattroff(win, COLOR_PAIR(color_pair) | A_BOLD);

    /* System count */
    wattron(win, A_DIM);
    wprintw(win, " (%d)", sys_count);
    wattroff(win, A_DIM);

    if (is_cursor) wattroff(win, A_REVERSE);
}

void tree_view_render(tree_view_t *tv, WINDOW *win) {
    if (!tv->rows || tv->row_count == 0) {
        wattron(win, A_DIM);
        mvwprintw(win, 1, 2, "No entities");
        wattroff(win, A_DIM);
        return;
    }

    int max_rows = getmaxy(win) - 2;
    int max_cols = getmaxx(win) - 2;

    tv->scroll.visible_rows = max_rows;
    scroll_ensure_visible(&tv->scroll);

    for (int i = 0; i < max_rows; i++) {
        int item_idx = tv->scroll.scroll_offset + i;
        if (item_idx >= tv->row_count) break;

        int win_row = i + 1;  /* +1 for top border */
        display_row_t *dr = &tv->rows[item_idx];
        bool is_cursor = (item_idx == tv->scroll.cursor);

        if (!dr->node) {
            if (dr->phase_group >= 0) {
                /* --- Phase sub-header row --- */
                const char *pname;
                int pcount;
                bool pcollapsed;
                if (dr->phase_group < tv->phase_count) {
                    pname = tv->phase_names[dr->phase_group];
                    pcount = tv->phase_system_counts[dr->phase_group];
                    pcollapsed = tv->phase_collapsed[dr->phase_group];
                } else {
                    /* Custom group sentinel */
                    pname = "Custom";
                    pcount = 0;
                    pcollapsed = false;
                }
                draw_phase_subheader(win, win_row, max_cols,
                                      pname, pcount,
                                      pcollapsed, is_cursor,
                                      phase_color_pair(pname));
            } else {
                /* --- Section header row --- */
                int s = dr->section_idx;
                draw_section_header(win, win_row, max_cols,
                                    section_names[s], tv->section_item_count[s],
                                    tv->section_collapsed[s], is_cursor);
            }
            continue;
        }

        /* --- Entity row --- */
        entity_node_t *node = dr->node;
        int col = 1;

        /* Disabled systems render entirely dimmed */
        bool dim_row = (node->entity_class == ENTITY_CLASS_SYSTEM && node->disabled);
        if (dim_row) wattron(win, A_DIM);

        if (is_cursor) {
            wattron(win, A_REVERSE);
            wmove(win, win_row, col);
            for (int c = 0; c < max_cols && c + col < getmaxx(win) - 1; c++) {
                waddch(win, ' ');
            }
        }

        /* Tree indentation */
        for (int d = 0; d < node->depth; d++) {
            if (col + 4 > max_cols + 1) break;

            if (d < node->depth - 1) {
                if (ancestor_has_next_sibling(node, d)) {
                    wattron(win, COLOR_PAIR(CP_TREE_LINE) | A_DIM);
                    mvwprintw(win, win_row, col, TREE_VERT "   ");
                    wattroff(win, COLOR_PAIR(CP_TREE_LINE) | A_DIM);
                } else {
                    mvwprintw(win, win_row, col, "    ");
                }
            } else {
                wattron(win, COLOR_PAIR(CP_TREE_LINE) | A_DIM);
                if (node_is_last_child(node)) {
                    mvwprintw(win, win_row, col,
                              TREE_LAST TREE_HORIZ TREE_HORIZ " ");
                } else {
                    mvwprintw(win, win_row, col,
                              TREE_BRANCH TREE_HORIZ TREE_HORIZ " ");
                }
                wattroff(win, COLOR_PAIR(CP_TREE_LINE) | A_DIM);
            }
            col += 4;
        }

        /* Expand/collapse indicator for entities with children */
        if (node->child_count > 0) {
            mvwprintw(win, win_row, col, "%s ", node->expanded ? "v" : ">");
            col += 2;
        } else {
            /* Indent leaf nodes to align with expanded siblings */
            mvwprintw(win, win_row, col, "  ");
            col += 2;
        }

        /* Entity name or anonymous ID */
        if (node->is_anonymous) {
            wattron(win, A_DIM);
            mvwprintw(win, win_row, col, "#%llu", (unsigned long long)node->id);
            wattroff(win, A_DIM);
        } else {
            mvwprintw(win, win_row, col, "%s", node->name);
        }

        /* Right-aligned info based on entity class */
        if (node->entity_class == ENTITY_CLASS_SYSTEM && node->class_detail) {
            /* System row: color-coded [Phase] tag + match count */
            char info_buf[96];
            if (node->system_match_count > 0) {
                snprintf(info_buf, sizeof(info_buf), "[%s] (%d)",
                         node->class_detail, node->system_match_count);
            } else {
                snprintf(info_buf, sizeof(info_buf), "[%s]", node->class_detail);
            }
            int info_len = (int)strlen(info_buf);
            int info_col = max_cols - info_len;
            if (info_col > col + 2) {
                int cp = phase_color_pair(node->class_detail);
                wattron(win, COLOR_PAIR(cp));
                mvwprintw(win, win_row, info_col, "[%s]", node->class_detail);
                wattroff(win, COLOR_PAIR(cp));
                if (node->system_match_count > 0) {
                    wattron(win, A_DIM);
                    wprintw(win, " (%d)", node->system_match_count);
                    wattroff(win, A_DIM);
                }
            }
        } else if (node->class_detail) {
            /* Non-system class_detail (compositions, lifecycles, etc.) */
            char info_buf[64];
            snprintf(info_buf, sizeof(info_buf), "[%s]", node->class_detail);
            int info_len = (int)strlen(info_buf);
            int info_col = max_cols - info_len;
            if (info_col > col + 2) {
                wattron(win, COLOR_PAIR(CP_COMPONENT_HEADER) | A_DIM);
                mvwprintw(win, win_row, info_col, "%s", info_buf);
                wattroff(win, COLOR_PAIR(CP_COMPONENT_HEADER) | A_DIM);
            }
        } else if (node->component_count > 0) {
            char comp_buf[256];
            int buf_pos = 0;
            int show_count = node->component_count > 3 ? 3 : node->component_count;

            for (int c = 0; c < show_count; c++) {
                if (c > 0) {
                    buf_pos += snprintf(comp_buf + buf_pos,
                                        sizeof(comp_buf) - (size_t)buf_pos, ", ");
                }
                buf_pos += snprintf(comp_buf + buf_pos,
                                    sizeof(comp_buf) - (size_t)buf_pos,
                                    "%s", node->component_names[c]);
            }
            if (node->component_count > 3) {
                snprintf(comp_buf + buf_pos,
                         sizeof(comp_buf) - (size_t)buf_pos,
                         " +%d more", node->component_count - 3);
            }

            int comp_len = (int)strlen(comp_buf);
            int comp_col = max_cols - comp_len;
            if (comp_col > col + 2) {
                wattron(win, A_DIM);
                mvwprintw(win, win_row, comp_col, "%s", comp_buf);
                wattroff(win, A_DIM);
            }
        }

        if (is_cursor) {
            wattroff(win, A_REVERSE);
        }
        if (dim_row) wattroff(win, A_DIM);
    }
}
