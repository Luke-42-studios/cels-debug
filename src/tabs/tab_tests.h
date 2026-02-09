#ifndef CELS_DEBUG_TAB_TESTS_H
#define CELS_DEBUG_TAB_TESTS_H

#include "../tab_system.h"

void tab_tests_init(tab_t *self);
void tab_tests_fini(tab_t *self);
void tab_tests_draw(const tab_t *self, WINDOW *win, const void *app_state);
bool tab_tests_input(tab_t *self, int ch, void *app_state);

#endif /* CELS_DEBUG_TAB_TESTS_H */
