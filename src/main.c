/*
 * cels-debug -- Terminal-based ECS inspector for CELS applications
 * Main event loop: input -> poll -> render
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ncurses.h>

#include "http_client.h"
#include "json_parser.h"
#include "data_model.h"
#include "tab_system.h"
#include "tui.h"

#define POLL_INTERVAL_MS 500

static volatile int g_running = 1;

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    const char *url = "http://localhost:27750/stats/world";

    /* Initialize TUI first (registers signal handlers) */
    tui_init();

    /* Initialize HTTP client */
    CURL *curl = http_client_init();
    if (!curl) {
        tui_fini();
        fprintf(stderr, "ERROR: Failed to initialize HTTP client\n");
        return 1;
    }

    /* Initialize tab system (after tui_init and http_client_init) */
    tab_system_t tabs;
    tab_system_init(&tabs);

    /* State */
    app_state_t app_state = {0};  /* snapshot=NULL, conn_state=CONN_DISCONNECTED */
    int64_t last_poll = 0;

    /* Main loop */
    while (g_running) {
        /* Step 1: Input -- global keys first, then tab switching, then per-tab */
        int ch = getch();

        if (ch == 'q' || ch == 'Q') {
            g_running = 0;
            continue;
        }

        if (ch == KEY_RESIZE) {
            tui_resize();
        }

        if (ch >= '1' && ch <= '6') {
            tab_system_activate(&tabs, ch - '1');
        } else if (ch == '\t') {
            tab_system_next(&tabs);
        } else if (ch != ERR) {
            tab_system_handle_input(&tabs, ch, &app_state);
        }

        /* Step 2: Poll on timer -- always poll /stats/world for connection health.
         * Only update snapshot data if the active tab needs ENDPOINT_STATS_WORLD. */
        int64_t now = now_ms();
        if (now - last_poll >= POLL_INTERVAL_MS) {
            uint32_t needed = tab_system_required_endpoints(&tabs);

            http_response_t resp = http_get(curl, url);
            app_state.conn_state =
                connection_state_update(app_state.conn_state, resp.status);

            /* Only parse and store snapshot if active tab needs world stats */
            if ((needed & ENDPOINT_STATS_WORLD) &&
                resp.status == 200 && resp.body.data) {
                world_snapshot_t *new_snap =
                    json_parse_world_stats(resp.body.data, resp.body.size);
                if (new_snap) {
                    world_snapshot_free(app_state.snapshot);
                    app_state.snapshot = new_snap;
                }
            }

            http_response_free(&resp);
            last_poll = now;

            /* TODO: Future phases add conditional polling for other endpoints
             * (ENDPOINT_STATS_PIPELINE, ENDPOINT_QUERY, etc.) here. */
        }

        /* Step 3: Render */
        tui_render(&tabs, &app_state);
    }

    /* Cleanup -- reverse of init order */
    tab_system_fini(&tabs);
    world_snapshot_free(app_state.snapshot);
    http_client_fini(curl);
    tui_fini();

    return 0;
}
