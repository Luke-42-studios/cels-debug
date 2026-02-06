#ifndef CELS_DEBUG_TAB_SYSTEMS_H
#define CELS_DEBUG_TAB_SYSTEMS_H

#include "../tab_system.h"

void tab_systems_init(tab_t *self);
void tab_systems_fini(tab_t *self);
void tab_systems_draw(const tab_t *self, WINDOW *win, const void *app_state);
bool tab_systems_input(tab_t *self, int ch, void *app_state);

#endif /* CELS_DEBUG_TAB_SYSTEMS_H */
