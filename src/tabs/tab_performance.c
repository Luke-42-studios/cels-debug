#define _POSIX_C_SOURCE 200809L

#include "tab_performance.h"
#include "../tui.h"
#include "../data_model.h"
#include "../scroll.h"
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Canonical Flecs pipeline phase execution order.
 * Keep in sync with tree_view.c phase_color_pair(). */
static const char *PHASE_ORDER[] = {
    "OnStart", "OnLoad", "PostLoad", "PreUpdate", "OnUpdate",
    "OnValidate", "PostUpdate", "PreStore", "OnStore", "PostFrame",
};
static const int PHASE_ORDER_COUNT = 10;

/* Phase name to color pair. Duplicated from tree_view.c for self-contained use.
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

/* Per-tab private state */
typedef struct perf_state {
    scroll_state_t scroll;
} perf_state_t;

/* --- Tag helpers for self-contained phase detection --- */

static bool has_tag_str(entity_node_t *node, const char *tag_name) {
    for (int i = 0; i < node->tag_count; i++) {
        if (node->tags[i] && strstr(node->tags[i], tag_name)) return true;
    }
    return false;
}

static char *extract_phase_from_tags(entity_node_t *node) {
    for (int i = 0; i < node->tag_count; i++) {
        if (node->tags[i] && strncmp(node->tags[i], "flecs.pipeline.", 15) == 0) {
            const char *phase = node->tags[i] + 15;
            if (phase[0] != '\0') return strdup(phase);
        }
    }
    return NULL;
}

/* --- Performance entry for waterfall rendering --- */

typedef struct perf_entry {
    const char *name;       /* system name (borrowed from system_registry) */
    char *phase;            /* phase name (owned, from entity tags) */
    double time_ms;         /* execution time in ms */
    int color;              /* color pair for this phase */
    bool disabled;          /* system disabled flag */
} perf_entry_t;

/* --- Phase group for sorted rendering --- */

typedef struct phase_group {
    const char *phase_name; /* borrowed from PHASE_ORDER or "Custom" */
    int color;
    perf_entry_t *entries;
    int count;
    int capacity;
    double total_time;
} phase_group_t;

static void phase_group_add(phase_group_t *pg, perf_entry_t entry) {
    if (pg->count >= pg->capacity) {
        int new_cap = pg->capacity == 0 ? 8 : pg->capacity * 2;
        perf_entry_t *new_entries = realloc(pg->entries,
            (size_t)new_cap * sizeof(perf_entry_t));
        if (!new_entries) return;
        pg->entries = new_entries;
        pg->capacity = new_cap;
    }
    pg->entries[pg->count++] = entry;
    pg->total_time += entry.time_ms;
}

/* --- Lifecycle --- */

void tab_performance_init(tab_t *self) {
    perf_state_t *ps = calloc(1, sizeof(perf_state_t));
    if (!ps) return;
    scroll_reset(&ps->scroll);
    self->state = ps;
}

void tab_performance_fini(tab_t *self) {
    perf_state_t *ps = (perf_state_t *)self->state;
    free(ps);
    self->state = NULL;
}

/* --- Draw --- */

void tab_performance_draw(const tab_t *self, WINDOW *win,
                          const void *app_state) {
    perf_state_t *ps = (perf_state_t *)self->state;
    if (!ps) return;

    const app_state_t *state = (const app_state_t *)app_state;

    werase(win);

    int max_y = getmaxy(win);
    int max_x = getmaxx(win);

    /* No data: show waiting message */
    if (!state->system_registry || state->system_registry->count == 0) {
        const char *msg = "Waiting for pipeline data...";
        int msg_len = (int)strlen(msg);
        wattron(win, A_DIM);
        mvwprintw(win, max_y / 2, (max_x - msg_len) / 2, "%s", msg);
        wattroff(win, A_DIM);
        wnoutrefresh(win);
        return;
    }

    /* Build perf entries: for each system in registry, find phase from entity tags */
    system_registry_t *reg = state->system_registry;
    entity_list_t *elist = state->entity_list;

    /* Phase groups: one per PHASE_ORDER entry + 1 custom */
    phase_group_t groups[PHASE_ORDER_COUNT + 1];
    memset(groups, 0, sizeof(groups));
    for (int p = 0; p < PHASE_ORDER_COUNT; p++) {
        groups[p].phase_name = PHASE_ORDER[p];
        groups[p].color = phase_color_pair(PHASE_ORDER[p]);
    }
    groups[PHASE_ORDER_COUNT].phase_name = "Custom";
    groups[PHASE_ORDER_COUNT].color = CP_PHASE_CUSTOM;

    double max_time = 0.0;
    int total_systems = 0;
    double total_time = 0.0;

    for (int s = 0; s < reg->count; s++) {
        system_info_t *si = &reg->systems[s];
        if (!si->name) continue;

        /* Find phase from entity tags */
        char *phase = NULL;
        if (elist) {
            for (int e = 0; e < elist->root_count; e++) {
                entity_node_t *node = elist->roots[e];
                if (!node->name) continue;
                if (strcmp(node->name, si->name) != 0) continue;

                /* Check if this is a system entity */
                if (has_tag_str(node, "flecs.system.System")) {
                    phase = extract_phase_from_tags(node);
                }
                break;
            }
        }

        perf_entry_t entry;
        entry.name = si->name;
        entry.phase = phase;
        entry.time_ms = si->time_spent_ms;
        entry.disabled = si->disabled;

        /* Determine which group */
        int group_idx = PHASE_ORDER_COUNT; /* default: Custom */
        if (phase) {
            for (int p = 0; p < PHASE_ORDER_COUNT; p++) {
                if (strcmp(phase, PHASE_ORDER[p]) == 0) {
                    group_idx = p;
                    break;
                }
            }
        }
        entry.color = groups[group_idx].color;

        phase_group_add(&groups[group_idx], entry);

        if (si->time_spent_ms > max_time) max_time = si->time_spent_ms;
        total_time += si->time_spent_ms;
        total_systems++;
    }

    /* Calculate total virtual rows needed for scroll */
    int total_rows = 0;
    total_rows += 4; /* title + separator + fps + blank */

    for (int g = 0; g <= PHASE_ORDER_COUNT; g++) {
        if (groups[g].count == 0) continue;
        total_rows += 1; /* phase header */
        total_rows += groups[g].count; /* system rows */
        total_rows += 1; /* blank after group */
    }
    total_rows += 2; /* separator + summary */

    ps->scroll.total_items = total_rows;
    ps->scroll.visible_rows = max_y;
    scroll_ensure_visible(&ps->scroll);

    /* Layout constants */
    int name_col = 4;       /* system name indent */
    int name_width = 24;    /* max name display width */
    int time_width = 10;    /* "X.XXXms " */
    int bar_start = name_col + name_width;
    int bar_max = max_x - bar_start - time_width - 2;
    if (bar_max < 4) bar_max = 4;

    /* Render rows with scroll offset */
    int vrow = 0; /* virtual row counter */
    int offset = ps->scroll.scroll_offset;

    /* Helper macro: only draw if virtual row is in visible range */
    #define VROW_VISIBLE(vr) ((vr) >= offset && (vr) < offset + max_y)
    #define SCREEN_ROW(vr) ((vr) - offset)

    /* Row 0: Title */
    if (VROW_VISIBLE(vrow)) {
        wattron(win, A_BOLD);
        mvwprintw(win, SCREEN_ROW(vrow), 2, "Performance");
        wattroff(win, A_BOLD);
    }
    vrow++;

    /* Row 1: Separator */
    if (VROW_VISIBLE(vrow)) {
        wattron(win, A_DIM);
        wmove(win, SCREEN_ROW(vrow), 1);
        for (int x = 0; x < max_x - 2; x++) waddch(win, ACS_HLINE);
        wattroff(win, A_DIM);
    }
    vrow++;

    /* Row 2: FPS and frame time */
    if (VROW_VISIBLE(vrow)) {
        if (state->snapshot) {
            wattron(win, COLOR_PAIR(CP_LABEL));
            mvwprintw(win, SCREEN_ROW(vrow), 2, "FPS:");
            wattroff(win, COLOR_PAIR(CP_LABEL));
            wprintw(win, " %.1f", state->snapshot->fps);

            wattron(win, COLOR_PAIR(CP_LABEL));
            mvwprintw(win, SCREEN_ROW(vrow), 18, "Frame:");
            wattroff(win, COLOR_PAIR(CP_LABEL));
            wprintw(win, " %.2fms", state->snapshot->frame_time_ms);

            wattron(win, COLOR_PAIR(CP_LABEL));
            mvwprintw(win, SCREEN_ROW(vrow), 38, "Systems:");
            wattroff(win, COLOR_PAIR(CP_LABEL));
            wprintw(win, " %d", total_systems);
        } else {
            wattron(win, A_DIM);
            mvwprintw(win, SCREEN_ROW(vrow), 2, "No world stats available");
            wattroff(win, A_DIM);
        }
    }
    vrow++;

    /* Row 3: blank */
    vrow++;

    /* Phase groups */
    for (int g = 0; g <= PHASE_ORDER_COUNT; g++) {
        if (groups[g].count == 0) continue;

        /* Phase header row */
        if (VROW_VISIBLE(vrow)) {
            int sr = SCREEN_ROW(vrow);
            int cp = groups[g].color;

            wattron(win, COLOR_PAIR(cp) | A_BOLD);
            mvwprintw(win, sr, 2, "%s", groups[g].phase_name);
            wattroff(win, COLOR_PAIR(cp) | A_BOLD);

            wattron(win, A_DIM);
            wprintw(win, " (%d system%s, %.2fms)",
                    groups[g].count,
                    groups[g].count == 1 ? "" : "s",
                    groups[g].total_time);
            wattroff(win, A_DIM);
        }
        vrow++;

        /* System rows with timing bars */
        for (int i = 0; i < groups[g].count; i++) {
            if (VROW_VISIBLE(vrow)) {
                int sr = SCREEN_ROW(vrow);
                perf_entry_t *entry = &groups[g].entries[i];

                /* System name (indented, phase-colored) */
                if (entry->disabled) {
                    wattron(win, COLOR_PAIR(CP_SYSTEM_DISABLED));
                } else {
                    wattron(win, COLOR_PAIR(groups[g].color));
                }
                mvwprintw(win, sr, name_col, "%-*.*s",
                          name_width, name_width, entry->name);
                if (entry->disabled) {
                    wattroff(win, COLOR_PAIR(CP_SYSTEM_DISABLED));
                } else {
                    wattroff(win, COLOR_PAIR(groups[g].color));
                }

                /* Proportional timing bar */
                int bar_width = 0;
                if (max_time > 0.0 && entry->time_ms > 0.0) {
                    bar_width = (int)((entry->time_ms / max_time) * bar_max);
                    if (bar_width < 1) bar_width = 1; /* min 1 char for non-zero */
                }

                if (bar_width > 0) {
                    wattron(win, COLOR_PAIR(groups[g].color) | A_BOLD);
                    wmove(win, sr, bar_start);
                    for (int b = 0; b < bar_width; b++) {
                        waddch(win, ACS_HLINE);
                    }
                    wattroff(win, COLOR_PAIR(groups[g].color) | A_BOLD);
                }

                /* Time label right of bar */
                int label_col = bar_start + bar_width + 1;
                if (label_col < max_x - time_width) {
                    wattron(win, COLOR_PAIR(CP_JSON_NUMBER));
                    mvwprintw(win, sr, label_col, "%.3fms", entry->time_ms);
                    wattroff(win, COLOR_PAIR(CP_JSON_NUMBER));
                }
            }
            vrow++;
        }

        /* Blank after group */
        vrow++;
    }

    /* Bottom separator */
    if (VROW_VISIBLE(vrow)) {
        wattron(win, A_DIM);
        wmove(win, SCREEN_ROW(vrow), 1);
        for (int x = 0; x < max_x - 2; x++) waddch(win, ACS_HLINE);
        wattroff(win, A_DIM);
    }
    vrow++;

    /* Summary row */
    if (VROW_VISIBLE(vrow)) {
        int sr = SCREEN_ROW(vrow);
        wattron(win, A_BOLD);
        mvwprintw(win, sr, 2, "Total:");
        wattroff(win, A_BOLD);
        wprintw(win, " %d system%s, %.2fms/frame",
                total_systems,
                total_systems == 1 ? "" : "s",
                total_time);

        if (state->snapshot && state->snapshot->fps > 0.0) {
            double budget = 1000.0 / state->snapshot->fps;
            double usage = (budget > 0.0) ? (total_time / budget) * 100.0 : 0.0;
            wattron(win, A_DIM);
            wprintw(win, "  (%.0f%% of frame budget)", usage);
            wattroff(win, A_DIM);
        }
    }

    #undef VROW_VISIBLE
    #undef SCREEN_ROW

    /* Free owned phase strings in entries */
    for (int g = 0; g <= PHASE_ORDER_COUNT; g++) {
        for (int i = 0; i < groups[g].count; i++) {
            free(groups[g].entries[i].phase);
        }
        free(groups[g].entries);
    }

    wnoutrefresh(win);
}

/* --- Input --- */

bool tab_performance_input(tab_t *self, int ch, void *app_state) {
    perf_state_t *ps = (perf_state_t *)self->state;
    if (!ps) return false;
    (void)app_state;

    switch (ch) {
    case KEY_UP:
    case 'k':
        scroll_move(&ps->scroll, -1);
        return true;

    case KEY_DOWN:
    case 'j':
        scroll_move(&ps->scroll, +1);
        return true;

    case KEY_PPAGE:
        scroll_page(&ps->scroll, -1);
        return true;

    case KEY_NPAGE:
        scroll_page(&ps->scroll, +1);
        return true;

    case 'g':
        scroll_to_top(&ps->scroll);
        return true;

    case 'G':
        scroll_to_bottom(&ps->scroll);
        return true;
    }

    return false;
}
