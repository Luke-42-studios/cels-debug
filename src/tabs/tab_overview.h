#ifndef CELS_DEBUG_TAB_OVERVIEW_H
#define CELS_DEBUG_TAB_OVERVIEW_H

#include "../tab_system.h"

void tab_overview_init(tab_t *self);
void tab_overview_fini(tab_t *self);
void tab_overview_draw(const tab_t *self, WINDOW *win,
                       const void *app_state);
bool tab_overview_input(tab_t *self, int ch, void *app_state);

#endif /* CELS_DEBUG_TAB_OVERVIEW_H */
