#include "tab_overview.h"
#include "../tui.h"
#include <ncurses.h>
#include <string.h>

void tab_overview_init(tab_t *self) {
    self->state = NULL;
}

void tab_overview_fini(tab_t *self) {
    (void)self;
}

void tab_overview_draw(const tab_t *self, WINDOW *win,
                       const void *app_state) {
    (void)self;
    const app_state_t *state = (const app_state_t *)app_state;

    if (state->snapshot) {
        /* Dashboard: labels in cyan, values in default color */
        wattron(win, COLOR_PAIR(CP_LABEL));
        mvwprintw(win, 1, 2, "Entities:");
        wattroff(win, COLOR_PAIR(CP_LABEL));
        wprintw(win, "   %.0f", state->snapshot->entity_count);

        wattron(win, COLOR_PAIR(CP_LABEL));
        mvwprintw(win, 2, 2, "FPS:");
        wattroff(win, COLOR_PAIR(CP_LABEL));
        wprintw(win, "        %.1f", state->snapshot->fps);

        wattron(win, COLOR_PAIR(CP_LABEL));
        mvwprintw(win, 3, 2, "Frame time:");
        wattroff(win, COLOR_PAIR(CP_LABEL));
        wprintw(win, " %.2f ms", state->snapshot->frame_time_ms);

        wattron(win, COLOR_PAIR(CP_LABEL));
        mvwprintw(win, 4, 2, "Systems:");
        wattroff(win, COLOR_PAIR(CP_LABEL));
        wprintw(win, "    %.0f", state->snapshot->system_count);
    } else {
        /* No data yet -- center message */
        const char *msg = "Waiting for data...";
        int msg_len = (int)strlen(msg);
        int max_y = getmaxy(win);
        int max_x = getmaxx(win);
        mvwprintw(win, max_y / 2, (max_x - msg_len) / 2, "%s", msg);
    }

    wnoutrefresh(win);
}

bool tab_overview_input(tab_t *self, int ch, void *app_state) {
    (void)self;
    (void)ch;
    (void)app_state;
    return false;
}
