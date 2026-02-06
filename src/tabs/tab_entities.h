#ifndef CELS_DEBUG_TAB_ENTITIES_H
#define CELS_DEBUG_TAB_ENTITIES_H

#include "../tab_system.h"

void tab_entities_init(tab_t *self);
void tab_entities_fini(tab_t *self);
void tab_entities_draw(const tab_t *self, WINDOW *win, const void *app_state);
bool tab_entities_input(tab_t *self, int ch, void *app_state);

#endif /* CELS_DEBUG_TAB_ENTITIES_H */
