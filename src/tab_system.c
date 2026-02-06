#include "tab_system.h"
#include "tabs/tab_placeholder.h"

/* Tab definitions (static, const) -- all 6 tabs use placeholder for now.
 * Overview tab will be replaced in Plan 02-02 with tab_overview. */
static const tab_def_t tab_defs[TAB_COUNT] = {
    { "Overview",     ENDPOINT_STATS_WORLD,
      tab_placeholder_init, tab_placeholder_fini,
      tab_placeholder_draw, tab_placeholder_input },
    { "Entities",     ENDPOINT_QUERY,
      tab_placeholder_init, tab_placeholder_fini,
      tab_placeholder_draw, tab_placeholder_input },
    { "Components",   ENDPOINT_COMPONENTS,
      tab_placeholder_init, tab_placeholder_fini,
      tab_placeholder_draw, tab_placeholder_input },
    { "Systems",      ENDPOINT_STATS_PIPELINE,
      tab_placeholder_init, tab_placeholder_fini,
      tab_placeholder_draw, tab_placeholder_input },
    { "State",        ENDPOINT_NONE,
      tab_placeholder_init, tab_placeholder_fini,
      tab_placeholder_draw, tab_placeholder_input },
    { "Performance",  ENDPOINT_STATS_WORLD | ENDPOINT_STATS_PIPELINE,
      tab_placeholder_init, tab_placeholder_fini,
      tab_placeholder_draw, tab_placeholder_input },
};

void tab_system_init(tab_system_t *ts) {
    ts->active = 0;
    for (int i = 0; i < TAB_COUNT; i++) {
        ts->tabs[i].def = &tab_defs[i];
        ts->tabs[i].state = NULL;
        if (ts->tabs[i].def->init) {
            ts->tabs[i].def->init(&ts->tabs[i]);
        }
    }
}

void tab_system_fini(tab_system_t *ts) {
    for (int i = 0; i < TAB_COUNT; i++) {
        if (ts->tabs[i].def->fini) {
            ts->tabs[i].def->fini(&ts->tabs[i]);
        }
    }
}

void tab_system_activate(tab_system_t *ts, int index) {
    if (index >= 0 && index < TAB_COUNT) {
        ts->active = index;
    }
}

void tab_system_next(tab_system_t *ts) {
    ts->active = (ts->active + 1) % TAB_COUNT;
}

bool tab_system_handle_input(tab_system_t *ts, int ch, void *app_state) {
    tab_t *active = &ts->tabs[ts->active];
    if (active->def->handle_input) {
        return active->def->handle_input(active, ch, app_state);
    }
    return false;
}

void tab_system_draw(const tab_system_t *ts, WINDOW *win,
                     const void *app_state) {
    const tab_t *active = &ts->tabs[ts->active];
    if (active->def->draw) {
        active->def->draw(active, win, app_state);
    }
}

uint32_t tab_system_required_endpoints(const tab_system_t *ts) {
    return ts->tabs[ts->active].def->required_endpoints;
}
