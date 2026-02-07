#include "tab_placeholder.h"
#include <string.h>

void tab_placeholder_init(tab_t *self) {
    self->state = NULL;
}

void tab_placeholder_fini(tab_t *self) {
    (void)self;
}

void tab_placeholder_draw(const tab_t *self, WINDOW *win,
                          const void *app_state) {
    (void)app_state;

    int max_y = getmaxy(win);
    int max_x = getmaxx(win);

    /* Center "Not implemented yet" */
    const char *msg = "Not implemented yet";
    int msg_len = (int)strlen(msg);
    mvwprintw(win, max_y / 2, (max_x - msg_len) / 2, "%s", msg);

    /* Show tab name below in dim text */
    const char *name = self->def->name;
    int name_len = (int)strlen(name);
    wattron(win, A_DIM);
    mvwprintw(win, max_y / 2 + 1, (max_x - name_len) / 2, "%s", name);
    wattroff(win, A_DIM);

    wnoutrefresh(win);
}

bool tab_placeholder_input(tab_t *self, int ch, void *app_state) {
    (void)self;
    (void)ch;
    (void)app_state;
    return false;
}
