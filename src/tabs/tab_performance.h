#ifndef CELS_DEBUG_TAB_PERFORMANCE_H
#define CELS_DEBUG_TAB_PERFORMANCE_H

#include "../tab_system.h"

void tab_performance_init(tab_t *self);
void tab_performance_fini(tab_t *self);
void tab_performance_draw(const tab_t *self, WINDOW *win,
                          const void *app_state);
bool tab_performance_input(tab_t *self, int ch, void *app_state);

#endif /* CELS_DEBUG_TAB_PERFORMANCE_H */
