#include "json_render.h"
#include "tui.h"
#include <string.h>

int json_render_value(WINDOW *win, yyjson_val *val, int row, int col,
                      int max_row, int col_width) {
    if (row >= max_row) return 0;
    if (!val) return 0;

    if (yyjson_is_null(val)) {
        wattron(win, A_DIM);
        mvwprintw(win, row, col, "null");
        wattroff(win, A_DIM);
        return 1;
    }

    if (yyjson_is_bool(val)) {
        wattron(win, COLOR_PAIR(CP_JSON_NUMBER));
        mvwprintw(win, row, col, "%s", yyjson_get_bool(val) ? "true" : "false");
        wattroff(win, COLOR_PAIR(CP_JSON_NUMBER));
        return 1;
    }

    if (yyjson_is_int(val)) {
        wattron(win, COLOR_PAIR(CP_JSON_NUMBER));
        mvwprintw(win, row, col, "%lld", (long long)yyjson_get_int(val));
        wattroff(win, COLOR_PAIR(CP_JSON_NUMBER));
        return 1;
    }

    if (yyjson_is_real(val)) {
        wattron(win, COLOR_PAIR(CP_JSON_NUMBER));
        mvwprintw(win, row, col, "%.4g", yyjson_get_real(val));
        wattroff(win, COLOR_PAIR(CP_JSON_NUMBER));
        return 1;
    }

    if (yyjson_is_str(val)) {
        int max_len = col_width - col - 2;
        if (max_len < 1) max_len = 1;
        wattron(win, COLOR_PAIR(CP_JSON_STRING));
        mvwprintw(win, row, col, "\"%.*s\"", max_len, yyjson_get_str(val));
        wattroff(win, COLOR_PAIR(CP_JSON_STRING));
        return 1;
    }

    if (yyjson_is_obj(val)) {
        int rows_used = 0;
        size_t idx, max;
        yyjson_val *key, *child;
        yyjson_obj_foreach(val, idx, max, key, child) {
            if (row + rows_used >= max_row) break;

            /* Print key name */
            wattron(win, COLOR_PAIR(CP_JSON_KEY));
            mvwprintw(win, row + rows_used, col, "%s", yyjson_get_str(key));
            wattroff(win, COLOR_PAIR(CP_JSON_KEY));
            wprintw(win, ": ");

            if (yyjson_is_obj(child) || yyjson_is_arr(child)) {
                rows_used++;
                rows_used += json_render_value(win, child, row + rows_used,
                                               col + 2, max_row, col_width);
            } else {
                /* Render simple value on same line */
                int cur_x = getcurx(win);
                rows_used += json_render_value(win, child, row + rows_used,
                                               cur_x, max_row, col_width);
            }
        }
        return rows_used;
    }

    if (yyjson_is_arr(val)) {
        int rows_used = 0;
        size_t idx, max;
        yyjson_val *elem;
        yyjson_arr_foreach(val, idx, max, elem) {
            if (row + rows_used >= max_row) break;

            mvwprintw(win, row + rows_used, col, "[%zu]:", idx);

            if (yyjson_is_obj(elem) || yyjson_is_arr(elem)) {
                rows_used++;
                rows_used += json_render_value(win, elem, row + rows_used,
                                               col + 2, max_row, col_width);
            } else {
                wprintw(win, " ");
                int cur_x = getcurx(win);
                rows_used += json_render_value(win, elem, row + rows_used,
                                               cur_x, max_row, col_width);
            }
        }
        return rows_used;
    }

    return 0;
}

int json_render_component(WINDOW *win, const char *comp_name, yyjson_val *comp_val,
                          int row, int col, int max_row, int col_width,
                          bool expanded) {
    if (row >= max_row) return 0;

    int rows_used = 0;

    /* Component header with expand/collapse indicator */
    wattron(win, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
    mvwprintw(win, row, col, "%s %s", expanded ? "v" : ">", comp_name);
    wattroff(win, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
    rows_used++;

    /* Render component value if expanded */
    if (expanded && comp_val && !yyjson_is_null(comp_val)) {
        rows_used += json_render_value(win, comp_val, row + rows_used,
                                       col + 2, max_row, col_width);
    }

    return rows_used;
}
