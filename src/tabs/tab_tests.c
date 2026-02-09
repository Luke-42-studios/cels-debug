#define _POSIX_C_SOURCE 200809L

#include "tab_tests.h"
#include "../tui.h"
#include "../split_panel.h"
#include "../scroll.h"
#include "../data_model.h"
#include "../json_parser.h"
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* --- Display entry: flat list row (suite header or test) --- */

typedef struct {
    bool is_header;          /* true = suite header row */
    const char *suite_name;  /* suite name (for headers and tests) */
    int suite_passed;        /* pass count (headers only) */
    int suite_total;         /* total count (headers only) */
    int test_index;          /* index into report->tests (-1 for headers) */
} test_display_entry_t;

/* Per-tab private state */
typedef struct tests_state {
    split_panel_t panel;
    scroll_state_t left_scroll;
    bool panel_created;

    /* Flat display list */
    test_display_entry_t *entries;
    int entry_count;
    int entry_capacity;

    /* Track if data has been loaded */
    bool data_loaded;
} tests_state_t;

/* --- Build flat display list grouped by suite --- */

static void rebuild_display_list(tests_state_t *ts, test_report_t *report) {
    ts->entry_count = 0;
    if (!report || report->test_count == 0) return;

    /* Collect unique suites in order of first appearance */
    const char *suites[128];
    int suite_passed[128];
    int suite_total[128];
    int suite_count = 0;

    for (int i = 0; i < report->test_count; i++) {
        const char *s = report->tests[i].suite ? report->tests[i].suite : "default";
        bool found = false;
        for (int j = 0; j < suite_count; j++) {
            if (strcmp(suites[j], s) == 0) {
                suite_total[j]++;
                if (report->tests[i].status == 0) suite_passed[j]++;
                found = true;
                break;
            }
        }
        if (!found && suite_count < 128) {
            suites[suite_count] = s;
            suite_total[suite_count] = 1;
            suite_passed[suite_count] = (report->tests[i].status == 0) ? 1 : 0;
            suite_count++;
        }
    }

    /* Calculate total entries needed: headers + tests */
    int total = suite_count + report->test_count;

    /* Ensure capacity */
    if (total > ts->entry_capacity) {
        test_display_entry_t *new_entries = realloc(ts->entries,
            (size_t)total * sizeof(test_display_entry_t));
        if (!new_entries) return;
        ts->entries = new_entries;
        ts->entry_capacity = total;
    }

    int idx = 0;

    for (int s = 0; s < suite_count; s++) {
        /* Suite header */
        ts->entries[idx].is_header = true;
        ts->entries[idx].suite_name = suites[s];
        ts->entries[idx].suite_passed = suite_passed[s];
        ts->entries[idx].suite_total = suite_total[s];
        ts->entries[idx].test_index = -1;
        idx++;

        /* Tests in this suite */
        for (int i = 0; i < report->test_count; i++) {
            const char *test_suite = report->tests[i].suite ? report->tests[i].suite : "default";
            if (strcmp(test_suite, suites[s]) != 0) continue;

            ts->entries[idx].is_header = false;
            ts->entries[idx].suite_name = suites[s];
            ts->entries[idx].suite_passed = 0;
            ts->entries[idx].suite_total = 0;
            ts->entries[idx].test_index = i;
            idx++;
        }
    }

    ts->entry_count = idx;
}

/* --- File reading helper --- */

static char *read_file_contents(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t read = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[read] = '\0';
    if (out_len) *out_len = read;
    return buf;
}

/* --- Load test report from disk --- */

static void load_test_report(app_state_t *state) {
    if (!state->test_json_path) return;

    /* Free existing report */
    test_report_free(state->test_report);
    state->test_report = NULL;

    /* Read and parse latest.json */
    size_t len = 0;
    char *json = read_file_contents(state->test_json_path, &len);
    if (!json) return;

    state->test_report = json_parse_test_report(json, len);
    free(json);

    /* Also try to load baseline */
    if (state->test_report && state->baseline_json_path) {
        json = read_file_contents(state->baseline_json_path, &len);
        if (json) {
            json_parse_bench_baseline(json, len, state->test_report);
            free(json);
        }
    }
}

/* --- Find baseline benchmark by name --- */

static bench_result_t *find_baseline(test_report_t *report, const char *name) {
    if (!report || !report->baseline || !name) return NULL;
    for (int i = 0; i < report->baseline_count; i++) {
        if (report->baseline[i].name && strcmp(report->baseline[i].name, name) == 0) {
            return &report->baseline[i];
        }
    }
    return NULL;
}

/* --- Format duration for display --- */

static void format_duration(int64_t ns, char *buf, size_t bufsz) {
    if (ns < 1000) {
        snprintf(buf, bufsz, "%lld ns", (long long)ns);
    } else if (ns < 1000000) {
        snprintf(buf, bufsz, "%.1f us", (double)ns / 1000.0);
    } else if (ns < 1000000000LL) {
        snprintf(buf, bufsz, "%.1f ms", (double)ns / 1000000.0);
    } else {
        snprintf(buf, bufsz, "%.2f s", (double)ns / 1000000000.0);
    }
}

/* --- Draw right panel: test detail --- */

static void draw_test_detail(WINDOW *rwin, int rh, int rw,
                              test_report_t *report, int test_index) {
    test_result_t *t = &report->tests[test_index];
    int row = 1;

    /* Title */
    wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
    mvwprintw(rwin, row, 1, "%.*s", rw, t->name ? t->name : "(unnamed)");
    wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
    row++;

    /* Separator */
    wattron(rwin, A_DIM);
    wmove(rwin, row, 1);
    for (int x = 0; x < rw; x++) waddch(rwin, ACS_HLINE);
    wattroff(rwin, A_DIM);
    row += 2;

    /* Suite */
    wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
    mvwprintw(rwin, row, 2, "Suite");
    wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
    mvwprintw(rwin, row, 16, "%s", t->suite ? t->suite : "default");
    row++;

    /* Status */
    wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
    mvwprintw(rwin, row, 2, "Status");
    wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
    if (t->status == 0) {
        wattron(rwin, COLOR_PAIR(CP_TEST_PASSED) | A_BOLD);
        mvwprintw(rwin, row, 16, "PASSED");
        wattroff(rwin, COLOR_PAIR(CP_TEST_PASSED) | A_BOLD);
    } else if (t->status == 1) {
        wattron(rwin, COLOR_PAIR(CP_TEST_FAILED) | A_BOLD);
        mvwprintw(rwin, row, 16, "FAILED");
        wattroff(rwin, COLOR_PAIR(CP_TEST_FAILED) | A_BOLD);
    } else {
        wattron(rwin, COLOR_PAIR(CP_RECONNECTING) | A_BOLD);
        mvwprintw(rwin, row, 16, "SKIPPED");
        wattroff(rwin, COLOR_PAIR(CP_RECONNECTING) | A_BOLD);
    }
    row++;

    /* Duration */
    wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
    mvwprintw(rwin, row, 2, "Duration");
    wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
    char dur_buf[64];
    format_duration(t->duration_ns, dur_buf, sizeof(dur_buf));
    wattron(rwin, COLOR_PAIR(CP_JSON_NUMBER));
    mvwprintw(rwin, row, 16, "%s", dur_buf);
    wattroff(rwin, COLOR_PAIR(CP_JSON_NUMBER));
    row++;

    /* Check if this is a benchmark test (suite == "bench") */
    if (t->suite && strcmp(t->suite, "bench") == 0 && t->name) {
        /* Find matching benchmark result */
        for (int b = 0; b < report->bench_count; b++) {
            if (!report->benchmarks[b].name) continue;
            if (strcmp(report->benchmarks[b].name, t->name) != 0) continue;

            bench_result_t *bench = &report->benchmarks[b];
            row += 2;

            wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
            mvwprintw(rwin, row, 1, "Benchmark Results");
            wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
            row++;

            /* Cycles */
            wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
            mvwprintw(rwin, row, 2, "Cycles");
            wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
            wattron(rwin, COLOR_PAIR(CP_JSON_NUMBER));
            mvwprintw(rwin, row, 16, "%llu", (unsigned long long)bench->cycles);
            wattroff(rwin, COLOR_PAIR(CP_JSON_NUMBER));
            row++;

            /* Wall time */
            wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
            mvwprintw(rwin, row, 2, "Wall time");
            wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
            wattron(rwin, COLOR_PAIR(CP_JSON_NUMBER));
            mvwprintw(rwin, row, 16, "%.1f us", bench->wall_ns / 1000.0);
            wattroff(rwin, COLOR_PAIR(CP_JSON_NUMBER));
            row++;

            /* Memory */
            if (bench->memory_bytes > 0) {
                wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
                mvwprintw(rwin, row, 2, "Memory");
                wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
                wattron(rwin, COLOR_PAIR(CP_JSON_NUMBER));
                mvwprintw(rwin, row, 16, "%llu bytes",
                          (unsigned long long)bench->memory_bytes);
                wattroff(rwin, COLOR_PAIR(CP_JSON_NUMBER));
                row++;
            }

            /* Baseline comparison */
            bench_result_t *base = find_baseline(report, bench->name);
            if (base && base->cycles > 0) {
                row++;
                wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
                mvwprintw(rwin, row, 1, "Baseline Comparison");
                wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
                row++;

                double pct = ((double)bench->cycles - (double)base->cycles)
                           / (double)base->cycles * 100.0;

                wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
                mvwprintw(rwin, row, 2, "Base cycles");
                wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
                wattron(rwin, COLOR_PAIR(CP_JSON_NUMBER));
                mvwprintw(rwin, row, 16, "%llu", (unsigned long long)base->cycles);
                wattroff(rwin, COLOR_PAIR(CP_JSON_NUMBER));
                row++;

                wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
                mvwprintw(rwin, row, 2, "Delta");
                wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));

                if (pct > 10.0) {
                    wattron(rwin, COLOR_PAIR(CP_BENCH_REGRESSION) | A_BOLD);
                    mvwprintw(rwin, row, 16, "%+.1f%% REGRESSION", pct);
                    wattroff(rwin, COLOR_PAIR(CP_BENCH_REGRESSION) | A_BOLD);
                } else if (pct < -10.0) {
                    wattron(rwin, COLOR_PAIR(CP_BENCH_IMPROVEMENT) | A_BOLD);
                    mvwprintw(rwin, row, 16, "%+.1f%% IMPROVEMENT", pct);
                    wattroff(rwin, COLOR_PAIR(CP_BENCH_IMPROVEMENT) | A_BOLD);
                } else {
                    wattron(rwin, COLOR_PAIR(CP_TEST_PASSED));
                    mvwprintw(rwin, row, 16, "%+.1f%% STABLE", pct);
                    wattroff(rwin, COLOR_PAIR(CP_TEST_PASSED));
                }
            }

            break;
        }
    }
}

/* --- Draw right panel: suite summary (when header selected) --- */

static void draw_suite_summary(WINDOW *rwin, int rh, int rw,
                                test_report_t *report,
                                const char *suite_name) {
    int row = 1;

    wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
    mvwprintw(rwin, row, 1, "%.*s", rw, suite_name);
    wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
    row++;

    /* Separator */
    wattron(rwin, A_DIM);
    wmove(rwin, row, 1);
    for (int x = 0; x < rw; x++) waddch(rwin, ACS_HLINE);
    wattroff(rwin, A_DIM);
    row += 2;

    /* Count tests in this suite */
    int total = 0, passed = 0, failed = 0, skipped_count = 0;
    for (int i = 0; i < report->test_count; i++) {
        const char *s = report->tests[i].suite ? report->tests[i].suite : "default";
        if (strcmp(s, suite_name) != 0) continue;
        total++;
        if (report->tests[i].status == 0) passed++;
        else if (report->tests[i].status == 1) failed++;
        else skipped_count++;
    }

    wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
    mvwprintw(rwin, row, 2, "Total");
    wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
    wattron(rwin, COLOR_PAIR(CP_JSON_NUMBER));
    mvwprintw(rwin, row, 16, "%d", total);
    wattroff(rwin, COLOR_PAIR(CP_JSON_NUMBER));
    row++;

    wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
    mvwprintw(rwin, row, 2, "Passed");
    wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
    wattron(rwin, COLOR_PAIR(CP_TEST_PASSED));
    mvwprintw(rwin, row, 16, "%d", passed);
    wattroff(rwin, COLOR_PAIR(CP_TEST_PASSED));
    row++;

    if (failed > 0) {
        wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
        mvwprintw(rwin, row, 2, "Failed");
        wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
        wattron(rwin, COLOR_PAIR(CP_TEST_FAILED));
        mvwprintw(rwin, row, 16, "%d", failed);
        wattroff(rwin, COLOR_PAIR(CP_TEST_FAILED));
        row++;
    }

    if (skipped_count > 0) {
        wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
        mvwprintw(rwin, row, 2, "Skipped");
        wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
        wattron(rwin, COLOR_PAIR(CP_RECONNECTING));
        mvwprintw(rwin, row, 16, "%d", skipped_count);
        wattroff(rwin, COLOR_PAIR(CP_RECONNECTING));
        row++;
    }

    /* Overall result bar */
    row++;
    if (failed > 0) {
        wattron(rwin, COLOR_PAIR(CP_TEST_FAILED) | A_BOLD);
        mvwprintw(rwin, row, 2, "FAIL");
        wattroff(rwin, COLOR_PAIR(CP_TEST_FAILED) | A_BOLD);
    } else {
        wattron(rwin, COLOR_PAIR(CP_TEST_PASSED) | A_BOLD);
        mvwprintw(rwin, row, 2, "ALL PASS");
        wattroff(rwin, COLOR_PAIR(CP_TEST_PASSED) | A_BOLD);
    }
}

/* --- Draw right panel: overall summary (no data or report summary) --- */

static void draw_report_summary(WINDOW *rwin, int rh, int rw,
                                 test_report_t *report) {
    int row = 1;

    wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
    mvwprintw(rwin, row, 1, "Test Report Summary");
    wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
    row++;

    wattron(rwin, A_DIM);
    wmove(rwin, row, 1);
    for (int x = 0; x < rw; x++) waddch(rwin, ACS_HLINE);
    wattroff(rwin, A_DIM);
    row += 2;

    if (report->version) {
        wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
        mvwprintw(rwin, row, 2, "Version");
        wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
        mvwprintw(rwin, row, 16, "%s", report->version);
        row++;
    }

    wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
    mvwprintw(rwin, row, 2, "Total");
    wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
    wattron(rwin, COLOR_PAIR(CP_JSON_NUMBER));
    mvwprintw(rwin, row, 16, "%d", report->total);
    wattroff(rwin, COLOR_PAIR(CP_JSON_NUMBER));
    row++;

    wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
    mvwprintw(rwin, row, 2, "Passed");
    wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
    wattron(rwin, COLOR_PAIR(CP_TEST_PASSED));
    mvwprintw(rwin, row, 16, "%d", report->passed);
    wattroff(rwin, COLOR_PAIR(CP_TEST_PASSED));
    row++;

    if (report->failed > 0) {
        wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
        mvwprintw(rwin, row, 2, "Failed");
        wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
        wattron(rwin, COLOR_PAIR(CP_TEST_FAILED));
        mvwprintw(rwin, row, 16, "%d", report->failed);
        wattroff(rwin, COLOR_PAIR(CP_TEST_FAILED));
        row++;
    }

    if (report->skipped > 0) {
        wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
        mvwprintw(rwin, row, 2, "Skipped");
        wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
        wattron(rwin, COLOR_PAIR(CP_RECONNECTING));
        mvwprintw(rwin, row, 16, "%d", report->skipped);
        wattroff(rwin, COLOR_PAIR(CP_RECONNECTING));
        row++;
    }

    if (report->bench_count > 0) {
        row++;
        wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
        mvwprintw(rwin, row, 2, "Benchmarks");
        wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
        wattron(rwin, COLOR_PAIR(CP_JSON_NUMBER));
        mvwprintw(rwin, row, 16, "%d", report->bench_count);
        wattroff(rwin, COLOR_PAIR(CP_JSON_NUMBER));
        row++;
    }

    /* Overall result */
    row++;
    if (report->failed > 0) {
        wattron(rwin, COLOR_PAIR(CP_TEST_FAILED) | A_BOLD);
        mvwprintw(rwin, row, 2, "SOME TESTS FAILED");
        wattroff(rwin, COLOR_PAIR(CP_TEST_FAILED) | A_BOLD);
    } else {
        wattron(rwin, COLOR_PAIR(CP_TEST_PASSED) | A_BOLD);
        mvwprintw(rwin, row, 2, "ALL %d TESTS PASSED", report->passed);
        wattroff(rwin, COLOR_PAIR(CP_TEST_PASSED) | A_BOLD);
    }
}

/* --- Lifecycle --- */

void tab_tests_init(tab_t *self) {
    tests_state_t *ts = calloc(1, sizeof(tests_state_t));
    if (!ts) return;

    scroll_reset(&ts->left_scroll);
    ts->panel_created = false;
    ts->entries = NULL;
    ts->entry_count = 0;
    ts->entry_capacity = 0;
    ts->data_loaded = false;

    self->state = ts;
}

void tab_tests_fini(tab_t *self) {
    tests_state_t *ts = (tests_state_t *)self->state;
    if (!ts) return;

    if (ts->panel_created) {
        split_panel_destroy(&ts->panel);
    }
    free(ts->entries);
    free(ts);
    self->state = NULL;
}

/* --- Draw --- */

void tab_tests_draw(const tab_t *self, WINDOW *win, const void *app_state) {
    tests_state_t *ts = (tests_state_t *)self->state;
    if (!ts) return;

    app_state_t *state = (app_state_t *)app_state;

    int h = getmaxy(win);
    int w = getmaxx(win);

    if (!ts->panel_created) {
        split_panel_create(&ts->panel, h, w, getbegy(win));
        ts->panel_created = true;
    } else if (h != ts->panel.height ||
               w != ts->panel.left_width + ts->panel.right_width) {
        split_panel_resize(&ts->panel, h, w, getbegy(win));
    }

    /* Auto-load data on first draw */
    if (!ts->data_loaded && state->test_json_path) {
        load_test_report(state);
        ts->data_loaded = true;
    }

    werase(ts->panel.left);
    werase(ts->panel.right);

    test_report_t *report = state->test_report;

    /* Build left panel title with suite count */
    char left_title[64];
    if (report) {
        /* Count unique suites */
        const char *seen[128];
        int suite_count = 0;
        for (int i = 0; i < report->test_count; i++) {
            const char *s = report->tests[i].suite ? report->tests[i].suite : "default";
            bool found = false;
            for (int j = 0; j < suite_count; j++) {
                if (strcmp(seen[j], s) == 0) { found = true; break; }
            }
            if (!found && suite_count < 128) seen[suite_count++] = s;
        }
        snprintf(left_title, sizeof(left_title), "Tests (%d/%d suites)",
                 suite_count, suite_count);
    } else {
        snprintf(left_title, sizeof(left_title), "Tests");
    }

    split_panel_draw_borders(&ts->panel, left_title, "Detail");

    /* --- Left panel: test list --- */
    if (report && report->test_count > 0) {
        rebuild_display_list(ts, report);

        int lh = getmaxy(ts->panel.left) - 2;
        int lw = getmaxx(ts->panel.left) - 2;
        ts->left_scroll.total_items = ts->entry_count;
        ts->left_scroll.visible_rows = lh;
        scroll_ensure_visible(&ts->left_scroll);

        for (int r = 0; r < lh; r++) {
            int idx = ts->left_scroll.scroll_offset + r;
            if (idx >= ts->entry_count) break;

            test_display_entry_t *entry = &ts->entries[idx];
            bool is_cursor = (idx == ts->left_scroll.cursor);
            int draw_row = r + 1;

            if (is_cursor) wattron(ts->panel.left, A_REVERSE);

            /* Clear row */
            wmove(ts->panel.left, draw_row, 1);
            for (int c = 0; c < lw; c++) waddch(ts->panel.left, ' ');

            if (entry->is_header) {
                /* Suite header */
                wattron(ts->panel.left, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
                mvwprintw(ts->panel.left, draw_row, 2, "%s", entry->suite_name);
                wattroff(ts->panel.left, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);

                /* Pass count */
                int cp = (entry->suite_passed == entry->suite_total)
                         ? CP_TEST_PASSED : CP_TEST_FAILED;
                wattron(ts->panel.left, COLOR_PAIR(cp));
                wprintw(ts->panel.left, " (%d/%d pass)",
                        entry->suite_passed, entry->suite_total);
                wattroff(ts->panel.left, COLOR_PAIR(cp));
            } else {
                /* Test entry */
                test_result_t *t = &report->tests[entry->test_index];
                const char *name = t->name ? t->name : "(unnamed)";

                wattron(ts->panel.left, COLOR_PAIR(CP_ENTITY_NAME));
                mvwprintw(ts->panel.left, draw_row, 4, "%.*s", lw - 12, name);
                wattroff(ts->panel.left, COLOR_PAIR(CP_ENTITY_NAME));

                /* Status tag on right */
                const char *tag;
                int tag_cp;
                if (t->status == 0) { tag = "PASS"; tag_cp = CP_TEST_PASSED; }
                else if (t->status == 1) { tag = "FAIL"; tag_cp = CP_TEST_FAILED; }
                else { tag = "SKIP"; tag_cp = CP_RECONNECTING; }

                int tag_col = lw - (int)strlen(tag);
                if (tag_col > 0) {
                    wattron(ts->panel.left, COLOR_PAIR(tag_cp) | A_BOLD);
                    mvwprintw(ts->panel.left, draw_row, tag_col, "%s", tag);
                    wattroff(ts->panel.left, COLOR_PAIR(tag_cp) | A_BOLD);
                }
            }

            if (is_cursor) wattroff(ts->panel.left, A_REVERSE);
        }
    } else {
        /* No data */
        int max_y = getmaxy(ts->panel.left);
        int max_x = getmaxx(ts->panel.left);
        wattron(ts->panel.left, A_DIM);
        if (!state->test_json_path) {
            mvwprintw(ts->panel.left, max_y / 2 - 1, 2,
                      "No test path configured.");
            mvwprintw(ts->panel.left, max_y / 2, 2,
                      "Use: cels-debug -t <path/to/latest.json>");
        } else {
            mvwprintw(ts->panel.left, max_y / 2 - 1,
                      (max_x - 30) / 2,
                      "No test results found.");
            mvwprintw(ts->panel.left, max_y / 2,
                      (max_x - 40) / 2,
                      "Run ./build/test_cels then press r.");
        }
        wattroff(ts->panel.left, A_DIM);
    }

    /* --- Right panel: context-sensitive detail --- */
    WINDOW *rwin = ts->panel.right;
    int rh = getmaxy(rwin) - 2;
    int rw = getmaxx(rwin) - 2;

    if (report && ts->entry_count > 0 && ts->left_scroll.cursor >= 0 &&
        ts->left_scroll.cursor < ts->entry_count) {
        test_display_entry_t *cur = &ts->entries[ts->left_scroll.cursor];

        if (cur->is_header) {
            draw_suite_summary(rwin, rh, rw, report, cur->suite_name);
        } else if (cur->test_index >= 0 && cur->test_index < report->test_count) {
            draw_test_detail(rwin, rh, rw, report, cur->test_index);
        }
    } else if (report) {
        draw_report_summary(rwin, rh, rw, report);
    }

    split_panel_refresh(&ts->panel);
}

/* --- Input --- */

bool tab_tests_input(tab_t *self, int ch, void *app_state) {
    tests_state_t *ts = (tests_state_t *)self->state;
    if (!ts) return false;

    app_state_t *state = (app_state_t *)app_state;

    /* Focus switching */
    if (split_panel_handle_focus(&ts->panel, ch)) return true;

    switch (ch) {
    case KEY_UP:
    case 'k':
        scroll_move(&ts->left_scroll, -1);
        return true;

    case KEY_DOWN:
    case 'j':
        scroll_move(&ts->left_scroll, +1);
        return true;

    case KEY_PPAGE:
        scroll_page(&ts->left_scroll, -1);
        return true;

    case KEY_NPAGE:
        scroll_page(&ts->left_scroll, +1);
        return true;

    case 'g':
        scroll_to_top(&ts->left_scroll);
        return true;

    case 'G':
        scroll_to_bottom(&ts->left_scroll);
        return true;

    case 'r':
        /* Refresh: reload from disk */
        load_test_report(state);
        ts->data_loaded = true;
        return true;
    }

    return false;
}
