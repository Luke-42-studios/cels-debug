#include "split_panel.h"
#include "tui.h"

void split_panel_create(split_panel_t *sp, int height, int width, int start_row) {
    sp->height = height;
    sp->start_row = start_row;
    sp->left_width = width * 40 / 100;
    sp->right_width = width - sp->left_width;
    sp->left = newwin(height, sp->left_width, start_row, 0);
    sp->right = newwin(height, sp->right_width, start_row, sp->left_width);
    sp->focus = 0;

    keypad(sp->left, TRUE);
    keypad(sp->right, TRUE);
}

void split_panel_destroy(split_panel_t *sp) {
    if (sp->left) {
        delwin(sp->left);
        sp->left = NULL;
    }
    if (sp->right) {
        delwin(sp->right);
        sp->right = NULL;
    }
}

void split_panel_resize(split_panel_t *sp, int height, int width, int start_row) {
    int saved_focus = sp->focus;
    split_panel_destroy(sp);
    split_panel_create(sp, height, width, start_row);
    sp->focus = saved_focus;
}

void split_panel_draw_borders(split_panel_t *sp, const char *left_title,
                               const char *right_title) {
    /* Left panel border */
    if (sp->focus == 0) {
        wattron(sp->left, A_BOLD | COLOR_PAIR(CP_PANEL_ACTIVE));
        box(sp->left, 0, 0);
        wattroff(sp->left, A_BOLD | COLOR_PAIR(CP_PANEL_ACTIVE));
    } else {
        wattron(sp->left, A_DIM | COLOR_PAIR(CP_PANEL_INACTIVE));
        box(sp->left, 0, 0);
        wattroff(sp->left, A_DIM | COLOR_PAIR(CP_PANEL_INACTIVE));
    }
    if (left_title) {
        mvwprintw(sp->left, 0, 2, " %s ", left_title);
    }

    /* Right panel border */
    if (sp->focus == 1) {
        wattron(sp->right, A_BOLD | COLOR_PAIR(CP_PANEL_ACTIVE));
        box(sp->right, 0, 0);
        wattroff(sp->right, A_BOLD | COLOR_PAIR(CP_PANEL_ACTIVE));
    } else {
        wattron(sp->right, A_DIM | COLOR_PAIR(CP_PANEL_INACTIVE));
        box(sp->right, 0, 0);
        wattroff(sp->right, A_DIM | COLOR_PAIR(CP_PANEL_INACTIVE));
    }
    if (right_title) {
        mvwprintw(sp->right, 0, 2, " %s ", right_title);
    }
}

void split_panel_refresh(split_panel_t *sp) {
    wnoutrefresh(sp->left);
    wnoutrefresh(sp->right);
}

WINDOW *split_panel_focused(split_panel_t *sp) {
    return (sp->focus == 0) ? sp->left : sp->right;
}

bool split_panel_handle_focus(split_panel_t *sp, int ch) {
    if (ch == KEY_LEFT) {
        sp->focus = 0;
        return true;
    }
    if (ch == KEY_RIGHT) {
        sp->focus = 1;
        return true;
    }
    return false;
}
