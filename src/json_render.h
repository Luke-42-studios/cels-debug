#ifndef CELS_DEBUG_JSON_RENDER_H
#define CELS_DEBUG_JSON_RENDER_H

#include <ncurses.h>
#include <stdbool.h>
#include <yyjson.h>

/*
 * Recursively render a yyjson value into an ncurses window.
 *
 * Handles: null, bool, int, real, string, object, array.
 * Objects and arrays are rendered with indentation.
 * Uses CP_JSON_KEY, CP_JSON_STRING, CP_JSON_NUMBER color pairs.
 *
 * Returns: number of rows consumed.
 */
int json_render_value(WINDOW *win, yyjson_val *val, int row, int col,
                      int max_row, int col_width);

/*
 * Render a component header with expand/collapse indicator, followed
 * by its JSON value if expanded.
 *
 * comp_name: displayed in CP_COMPONENT_HEADER with A_BOLD
 * comp_val:  yyjson value to render if expanded (may be NULL)
 * expanded:  whether to show the component's value
 *
 * Returns: total rows consumed (header + value if expanded).
 */
int json_render_component(WINDOW *win, const char *comp_name, yyjson_val *comp_val,
                          int row, int col, int max_row, int col_width, bool expanded);

#endif /* CELS_DEBUG_JSON_RENDER_H */
