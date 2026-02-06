#include "tui.h"
#include "tab_system.h"
#include <locale.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Windows -- 4-window layout: header, tab bar, content, footer */
static WINDOW *win_header  = NULL;
static WINDOW *win_tabbar  = NULL;
static WINDOW *win_content = NULL;
static WINDOW *win_footer  = NULL;

/* Track whether ncurses is initialized (for safe cleanup) */
static int g_ncurses_active = 0;

/* --- Signal handling --- */

static void signal_handler(int sig) {
    (void)sig;
    if (g_ncurses_active) {
        endwin();
    }
    _exit(1);
}

static void cleanup_atexit(void) {
    if (g_ncurses_active) {
        endwin();
        g_ncurses_active = 0;
    }
}

/* --- Window management --- */

static void create_windows(void) {
    /* Row 0:           header  (1 line)
     * Row 1:           tab bar (1 line)
     * Row 2..LINES-2:  content (LINES-3 lines)
     * Row LINES-1:     footer  (1 line) */
    win_header  = newwin(1, COLS, 0, 0);
    win_tabbar  = newwin(1, COLS, 1, 0);
    win_content = newwin(LINES - 3, COLS, 2, 0);
    win_footer  = newwin(1, COLS, LINES - 1, 0);
}

static void destroy_windows(void) {
    if (win_header)  { delwin(win_header);  win_header  = NULL; }
    if (win_tabbar)  { delwin(win_tabbar);  win_tabbar  = NULL; }
    if (win_content) { delwin(win_content); win_content = NULL; }
    if (win_footer)  { delwin(win_footer);  win_footer  = NULL; }
}

/* --- Public API --- */

void tui_init(void) {
    /* Required for Unicode box drawing characters with ncursesw */
    setlocale(LC_ALL, "");

    /* Signal handlers first -- protect terminal from crashes */
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);
    atexit(cleanup_atexit);

    /* ncurses init */
    initscr();
    g_ncurses_active = 1;
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(100);  /* getch returns ERR after 100ms */

    /* Color support */
    if (has_colors()) {
        start_color();
        use_default_colors();
        assume_default_colors(-1, -1);
        init_pair(CP_CONNECTED,    COLOR_GREEN,  -1);
        init_pair(CP_DISCONNECTED, COLOR_RED,    -1);
        init_pair(CP_RECONNECTING, COLOR_YELLOW, -1);
        init_pair(CP_LABEL,        COLOR_CYAN,   -1);
        init_pair(CP_TAB_ACTIVE,   COLOR_WHITE,  -1);
        init_pair(CP_TAB_INACTIVE, COLOR_WHITE,  -1);

        /* Phase 03: entity/component UI color pairs */
        init_pair(CP_TREE_LINE,        COLOR_WHITE,  -1);
        init_pair(CP_ENTITY_NAME,      COLOR_WHITE,  -1);
        init_pair(CP_COMPONENT_HEADER, COLOR_CYAN,   -1);
        init_pair(CP_JSON_KEY,         COLOR_CYAN,   -1);
        init_pair(CP_JSON_STRING,      COLOR_GREEN,  -1);
        init_pair(CP_JSON_NUMBER,      COLOR_YELLOW, -1);
        init_pair(CP_PANEL_ACTIVE,     COLOR_WHITE,  -1);
        init_pair(CP_PANEL_INACTIVE,   COLOR_WHITE,  -1);
        init_pair(CP_CURSOR,           COLOR_BLACK,  COLOR_WHITE);
    }

    /* Use terminal default background on stdscr */
    bkgd(COLOR_PAIR(0));

    create_windows();
}

void tui_fini(void) {
    destroy_windows();
    if (g_ncurses_active) {
        endwin();
        g_ncurses_active = 0;
    }
}

void tui_render(const tab_system_t *tabs, const app_state_t *state) {
    /* 1. Clear all windows */
    werase(win_header);
    werase(win_tabbar);
    werase(win_content);
    werase(win_footer);

    /* 2. Header: "cels-debug | <status>" */
    mvwprintw(win_header, 0, 1, "cels-debug");
    wprintw(win_header, " | ");

    switch (state->conn_state) {
    case CONN_CONNECTED:
        wattron(win_header, COLOR_PAIR(CP_CONNECTED) | A_BOLD);
        wprintw(win_header, "Connected");
        wattroff(win_header, COLOR_PAIR(CP_CONNECTED) | A_BOLD);
        break;
    case CONN_DISCONNECTED:
        wattron(win_header, COLOR_PAIR(CP_DISCONNECTED) | A_BOLD);
        wprintw(win_header, "Disconnected");
        wattroff(win_header, COLOR_PAIR(CP_DISCONNECTED) | A_BOLD);
        break;
    case CONN_RECONNECTING:
        wattron(win_header, COLOR_PAIR(CP_RECONNECTING) | A_BOLD);
        wprintw(win_header, "Reconnecting...");
        wattroff(win_header, COLOR_PAIR(CP_RECONNECTING) | A_BOLD);
        break;
    }

    /* 3. Tab bar: " N:Name " for each tab, active tab highlighted */
    {
        int col = 1;
        for (int i = 0; i < TAB_COUNT; i++) {
            const char *name = tabs->tabs[i].def->name;

            if (i == tabs->active) {
                wattron(win_tabbar,
                        A_REVERSE | A_BOLD | COLOR_PAIR(CP_TAB_ACTIVE));
                mvwprintw(win_tabbar, 0, col, " %d:%s ", i + 1, name);
                wattroff(win_tabbar,
                         A_REVERSE | A_BOLD | COLOR_PAIR(CP_TAB_ACTIVE));
            } else {
                wattron(win_tabbar, COLOR_PAIR(CP_TAB_INACTIVE));
                mvwprintw(win_tabbar, 0, col, " %d:%s ", i + 1, name);
                wattroff(win_tabbar, COLOR_PAIR(CP_TAB_INACTIVE));
            }

            /* Advance column past the label */
            col += snprintf(NULL, 0, " %d:%s ", i + 1, name);
        }
    }

    /* 4. Content: dispatch to active tab's draw function */
    tab_system_draw(tabs, win_content, state);

    /* 5. Footer: help text */
    mvwprintw(win_footer, 0, 1, "1-6:tabs  TAB:next  q:quit");

    /* 6. Batch refresh (no flicker) */
    wnoutrefresh(win_header);
    wnoutrefresh(win_tabbar);
    wnoutrefresh(win_content);
    wnoutrefresh(win_footer);
    doupdate();
}

void tui_resize(void) {
    endwin();
    refresh();
    destroy_windows();
    create_windows();
}
