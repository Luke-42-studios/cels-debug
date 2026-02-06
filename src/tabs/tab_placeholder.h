#ifndef CELS_DEBUG_TAB_PLACEHOLDER_H
#define CELS_DEBUG_TAB_PLACEHOLDER_H

#include "../tab_system.h"

void tab_placeholder_init(tab_t *self);
void tab_placeholder_fini(tab_t *self);
void tab_placeholder_draw(const tab_t *self, WINDOW *win,
                          const void *app_state);
bool tab_placeholder_input(tab_t *self, int ch, void *app_state);

#endif /* CELS_DEBUG_TAB_PLACEHOLDER_H */
