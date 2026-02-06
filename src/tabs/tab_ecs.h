#ifndef CELS_DEBUG_TAB_ECS_H
#define CELS_DEBUG_TAB_ECS_H

#include "../tab_system.h"

void tab_ecs_init(tab_t *self);
void tab_ecs_fini(tab_t *self);
void tab_ecs_draw(const tab_t *self, WINDOW *win, const void *app_state);
bool tab_ecs_input(tab_t *self, int ch, void *app_state);

#endif /* CELS_DEBUG_TAB_ECS_H */
