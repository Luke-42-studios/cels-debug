#ifndef CELS_DEBUG_TAB_COMPONENTS_H
#define CELS_DEBUG_TAB_COMPONENTS_H

#include "../tab_system.h"

void tab_components_init(tab_t *self);
void tab_components_fini(tab_t *self);
void tab_components_draw(const tab_t *self, WINDOW *win, const void *app_state);
bool tab_components_input(tab_t *self, int ch, void *app_state);

#endif /* CELS_DEBUG_TAB_COMPONENTS_H */
