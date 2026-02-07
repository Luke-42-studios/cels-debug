/*
 * cels-debug -- Terminal-based ECS inspector for CELS applications
 * Main event loop: input -> poll -> render
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ncurses.h>

#include "http_client.h"
#include "json_parser.h"
#include "data_model.h"
#include "tab_system.h"
#include "tui.h"

#define POLL_INTERVAL_MS 500

static volatile int g_running = 1;

/* Navigation back-stack helpers */
static void nav_push(nav_stack_t *stack, int tab, uint64_t entity_id) {
    if (stack->top < NAV_STACK_MAX - 1) {
        stack->top++;
        stack->entries[stack->top].tab_index = tab;
        stack->entries[stack->top].entity_id = entity_id;
    }
}

static bool nav_pop(nav_stack_t *stack, nav_entry_t *out) {
    if (stack->top < 0) return false;
    *out = stack->entries[stack->top];
    stack->top--;
    return true;
}

static void nav_clear(nav_stack_t *stack) {
    stack->top = -1;
}

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char *argv[]) {
    /* Parse command-line flags */
    int poll_interval = POLL_INTERVAL_MS;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            poll_interval = atoi(argv[++i]);
            if (poll_interval < 100) poll_interval = 100;    /* minimum 100ms */
            if (poll_interval > 5000) poll_interval = 5000;  /* maximum 5s */
        }
    }

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
    app_state.pending_tab = -1;
    app_state.nav_stack.top = -1;
    app_state.poll_interval_ms = poll_interval;
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

        /* Esc key: pop nav stack or pass to active tab */
        if (ch == 27) {
            nav_entry_t entry;
            if (nav_pop(&app_state.nav_stack, &entry)) {
                tab_system_activate(&tabs, entry.tab_index);
                /* Restore entity selection if possible */
                if (entry.entity_id != 0 && app_state.entity_list) {
                    for (int i = 0; i < app_state.entity_list->count; i++) {
                        entity_node_t *n = app_state.entity_list->nodes[i];
                        if (n->id == entry.entity_id && n->full_path) {
                            free(app_state.selected_entity_path);
                            app_state.selected_entity_path = strdup(n->full_path);
                            break;
                        }
                    }
                }
            } else {
                /* No nav history -- pass to tab */
                tab_system_handle_input(&tabs, ch, &app_state);
            }
        } else if (ch >= '1' && ch <= '0' + TAB_COUNT) {
            /* Direct tab switch -- clear nav stack (new context) */
            nav_clear(&app_state.nav_stack);
            tab_system_activate(&tabs, ch - '1');
        } else if (ch == '\t') {
            /* Tab key -- clear nav stack (new context) */
            nav_clear(&app_state.nav_stack);
            tab_system_next(&tabs);
        } else if (ch != ERR) {
            tab_system_handle_input(&tabs, ch, &app_state);
        }

        /* Cross-tab navigation (e.g., Systems tab -> CELS tab) */
        if (app_state.pending_tab >= 0) {
            /* Push current tab onto nav stack before switching */
            nav_push(&app_state.nav_stack, tabs.active, 0);
            tab_system_activate(&tabs, app_state.pending_tab);
            app_state.pending_tab = -1;
        }

        /* Step 2: Poll on timer -- always poll /stats/world for connection health.
         * Only update snapshot data if the active tab needs ENDPOINT_STATS_WORLD. */
        int64_t now = now_ms();
        if (now - last_poll >= app_state.poll_interval_ms) {
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

            /* Poll entity list if active tab needs it */
            if ((needed & ENDPOINT_QUERY) && app_state.conn_state == CONN_CONNECTED) {
                static const char *entity_list_url =
                    "http://localhost:27750/query"
                    "?expr=!ChildOf(self%7Cup%2Cflecs)%2C!Module(self%7Cup)"
                    "&entity_id=true&values=false&table=true&try=true";
                http_response_t qresp = http_get(curl, entity_list_url);
                if (qresp.status == 200 && qresp.body.data) {
                    entity_list_t *new_list =
                        json_parse_entity_list(qresp.body.data, qresp.body.size);
                    if (new_list) {
                        entity_list_free(app_state.entity_list);
                        app_state.entity_list = new_list;
                    }
                }
                http_response_free(&qresp);
            }

            /* Poll selected entity detail if an entity is selected */
            if ((needed & ENDPOINT_ENTITY) && app_state.selected_entity_path &&
                app_state.conn_state == CONN_CONNECTED) {
                char entity_url[512];
                snprintf(entity_url, sizeof(entity_url),
                    "http://localhost:27750/entity/%s?entity_id=true&try=true&doc=true",
                    app_state.selected_entity_path);
                http_response_t eresp = http_get(curl, entity_url);
                if (eresp.status == 200 && eresp.body.data) {
                    entity_detail_t *new_detail =
                        json_parse_entity_detail(eresp.body.data, eresp.body.size);
                    if (new_detail) {
                        entity_detail_free(app_state.entity_detail);
                        app_state.entity_detail = new_detail;
                    }
                } else if (eresp.status == 404 || eresp.status == -1) {
                    /* Entity was deleted -- clear detail and notify */
                    entity_detail_free(app_state.entity_detail);
                    app_state.entity_detail = NULL;
                    /* Set footer notification */
                    free(app_state.footer_message);
                    app_state.footer_message = strdup("Selected entity removed");
                    app_state.footer_message_expire = now + 3000; /* 3 seconds */
                    free(app_state.selected_entity_path);
                    app_state.selected_entity_path = NULL;
                }
                http_response_free(&eresp);
            }

            /* Poll component registry if active tab needs it */
            if ((needed & ENDPOINT_COMPONENTS) && app_state.conn_state == CONN_CONNECTED) {
                http_response_t cresp = http_get(curl,
                    "http://localhost:27750/components?try=true");
                if (cresp.status == 200 && cresp.body.data) {
                    component_registry_t *new_reg =
                        json_parse_component_registry(cresp.body.data, cresp.body.size);
                    if (new_reg) {
                        component_registry_free(app_state.component_registry);
                        app_state.component_registry = new_reg;
                    }
                }
                http_response_free(&cresp);
            }

            /* Poll pipeline stats if active tab needs it */
            if ((needed & ENDPOINT_STATS_PIPELINE) && app_state.conn_state == CONN_CONNECTED) {
                http_response_t presp = http_get(curl,
                    "http://localhost:27750/stats/pipeline");
                if (presp.status == 200 && presp.body.data) {
                    system_registry_t *new_reg =
                        json_parse_pipeline_stats(presp.body.data, presp.body.size);
                    if (new_reg) {
                        system_registry_free(app_state.system_registry);
                        app_state.system_registry = new_reg;
                    }
                }
                http_response_free(&presp);
            }

            /* Expire footer message */
            if (app_state.footer_message && now >= app_state.footer_message_expire) {
                free(app_state.footer_message);
                app_state.footer_message = NULL;
            }
        }

        /* Step 3: Render */
        tui_render(&tabs, &app_state);
    }

    /* Cleanup -- reverse of init order */
    tab_system_fini(&tabs);
    world_snapshot_free(app_state.snapshot);
    entity_list_free(app_state.entity_list);
    entity_detail_free(app_state.entity_detail);
    component_registry_free(app_state.component_registry);
    system_registry_free(app_state.system_registry);
    free(app_state.selected_entity_path);
    free(app_state.footer_message);
    http_client_fini(curl);
    tui_fini();

    return 0;
}
