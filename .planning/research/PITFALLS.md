# Domain Pitfalls

**Domain:** C99 TUI debugger with ncurses, HTTP polling, JSON parsing
**Researched:** 2026-02-05
**Confidence:** HIGH (ncurses/curl domains well-documented), MEDIUM (flecs REST specifics)

---

## Critical Pitfalls

Mistakes that cause rewrites, crashes, or corrupt terminal state.

---

### Pitfall 1: Blocking HTTP in the UI Thread

**What goes wrong:** Using `curl_easy_perform()` blocks the entire application while waiting for an HTTP response. If the flecs REST server is slow, unresponsive, or the target application has crashed, the TUI freezes completely -- no keyboard input, no screen updates, no resize handling. The user sees a hung terminal with no feedback.

**Why it happens:** `curl_easy_perform()` is the simplest libcurl API and the one every tutorial shows first. It performs the full request synchronously. Developers reach for it because it is straightforward, then discover too late that their UI is unresponsive during network calls.

**Consequences:**
- TUI appears frozen for seconds at a time during each poll
- Keyboard input is lost during the block
- Terminal resize events are missed
- If the target app crashes, the debugger hangs until the connection timeout expires
- Users think the debugger itself has crashed

**Prevention:**
- Use `curl_multi_perform()` with `curl_multi_poll()` for non-blocking HTTP, integrated into the main event loop
- Alternatively, use a dedicated network thread with a thread-safe queue to pass data to the UI thread (see Pitfall 3 for thread safety)
- Set `CURLOPT_CONNECTTIMEOUT_MS` to something short (500-1000ms) and `CURLOPT_TIMEOUT_MS` to cap total request time (2000-3000ms)
- Set `CURLOPT_NOSIGNAL` to 1L when using threads to prevent curl from installing signal handlers that conflict with ncurses

**Detection:** If getch() ever blocks for more than the configured poll interval, the HTTP client is blocking the UI thread.

**Which phase should address it:** Phase 1 (foundation). The event loop architecture must be decided at the start because it affects every other component. Retrofitting non-blocking HTTP into a blocking design requires a rewrite.

**Confidence:** HIGH -- curl documentation explicitly states this; well-documented problem.

**Sources:**
- [curl_easy_perform](https://curl.se/libcurl/c/curl_easy_perform.html)
- [libcurl multi interface](https://curl.se/libcurl/c/libcurl-multi.html)
- [curl thread safety](https://curl.se/libcurl/c/threadsafe.html)

---

### Pitfall 2: Calling wrefresh() Per Window (Flicker and Tearing)

**What goes wrong:** With 6+ tabs, each containing multiple ncurses windows (header bar, tab bar, content area, status bar), calling `wrefresh()` on each window individually causes visible flicker. Each `wrefresh()` call writes to the terminal immediately, producing multiple partial screen updates per frame. Users see tearing, flashing, and incomplete renders.

**Why it happens:** `wrefresh()` is the obvious function name and appears in every ncurses tutorial. It internally calls `wnoutrefresh()` then `doupdate()`, meaning each window triggers a full terminal write. With 6+ windows being updated per poll cycle, that is 6+ terminal writes per frame.

**Consequences:**
- Visible flickering/tearing on every data update
- Wasted CPU time and terminal bandwidth (each window triggers a separate diff + write)
- Looks unprofessional -- users associate flicker with buggy software

**Prevention:**
- Use `wnoutrefresh()` for every window, then call `doupdate()` exactly once per frame. This is ncurses' built-in double-buffering mechanism.
- Structure the render loop as: update all data, call `wnoutrefresh()` on each dirty window, call `doupdate()` once.
- Never mix `wrefresh()` and `wnoutrefresh()` in the same render pass.

**Detection:** Any visible flicker when data updates. Profile by counting `doupdate()` calls per frame -- should be exactly 1.

**Which phase should address it:** Phase 1 (foundation). Establish the render pattern from the start. Every tab and panel must use the same `wnoutrefresh()`/`doupdate()` discipline.

**Confidence:** HIGH -- documented in ncurses official documentation and multiple references.

**Sources:**
- [wrefresh man page](https://linux.die.net/man/3/wrefresh)
- [TLDP ncurses refresh docs](https://tldp.org/LDP/lpg/node114.html)

---

### Pitfall 3: ncurses Is Not Thread-Safe

**What goes wrong:** A network thread fetches data and directly calls ncurses functions to update the display. This corrupts ncurses' internal data structures, causing garbled output, segfaults, or terminal corruption. Even a `refresh()` from the wrong thread at the wrong moment can flush an inconsistent output buffer.

**Why it happens:** Developers think "I will use a thread for networking and another for UI" but then access ncurses from both threads. ncurses uses global state (stdscr, LINES, COLS, internal buffers) with zero internal synchronization. Even the "threaded" ncurses build (ncursest) only provides limited safety -- it does not make arbitrary concurrent access safe.

**Consequences:**
- Garbled terminal output
- Segmentation faults
- Corrupted terminal state requiring `reset` command
- Intermittent, hard-to-reproduce bugs

**Prevention:**
- ALL ncurses calls must happen from a single thread (the main/UI thread)
- If using a network thread, communicate via a thread-safe queue: network thread pushes data, UI thread polls the queue and does all rendering
- Alternatively, use single-threaded architecture with non-blocking I/O (`curl_multi` + `poll()`/`select()` on stdin)
- For the single-threaded approach: use `timeout(0)` or `nodelay(stdscr, TRUE)` for non-blocking getch, then `poll()` on stdin to detect when input is available

**Detection:** Any ncurses function call from a non-UI thread. Code review for `wprintw`, `mvwprintw`, `wrefresh`, `wnoutrefresh`, `doupdate`, `werase`, etc. outside the main thread.

**Which phase should address it:** Phase 1 (foundation). The threading model is an architectural decision that cannot be changed later without rewriting the entire application.

**Confidence:** HIGH -- ncurses threading documentation is explicit about this.

**Sources:**
- [curs_threads man page](https://man7.org/linux/man-pages/man3/curs_threads.3x.html)
- [ncurses threading docs](https://invisible-island.net/ncurses/man/curs_threads.3x.html)

---

### Pitfall 4: Terminal Left Corrupted on Crash/Signal

**What goes wrong:** If the debugger crashes, receives SIGINT (Ctrl+C), SIGTERM, or hits an assertion failure without calling `endwin()`, the terminal is left in raw/cbreak mode. The user's shell becomes unusable -- no echo, no line editing, wrong cursor position, escape sequences printed as garbage.

**Why it happens:** ncurses puts the terminal into raw mode (no echo, no line buffering, special character handling). If the program exits without calling `endwin()`, these settings persist. Signal handlers installed by ncurses have documented limitations -- they call functions that are not async-signal-safe (malloc, stdio).

**Consequences:**
- Terminal left in raw mode after crash
- User must type `reset` blindly to recover their shell
- During development, this happens frequently and is extremely frustrating
- On production use, this erodes trust in the tool

**Prevention:**
- Install signal handlers for SIGINT, SIGTERM, SIGSEGV, SIGABRT that call `endwin()` then `_exit()`
- Use `atexit(endwin)` as a safety net (works for normal exits, not for crashes from signals)
- Keep signal handlers minimal: set a flag, let the main loop check it and do clean shutdown
- For the safest approach: signal handler sets a volatile sig_atomic_t flag; main loop checks and calls endwin() + cleanup
- During development, wrap the main loop in a pattern that always calls `endwin()` even on error paths

**Detection:** Kill the debugger with `kill -9` during development. If terminal is corrupted, the cleanup path is incomplete.

**Which phase should address it:** Phase 1 (foundation). Signal handling must be set up alongside ncurses initialization. Add it as part of the init/shutdown pair.

**Confidence:** HIGH -- universal ncurses problem, extensively documented.

**Sources:**
- [ncurses initscr/endwin](https://invisible-island.net/ncurses/man/curs_initscr.3x.html)
- [ncurses signal limitations](https://invisible-island.net/ncurses/ncurses-intro.html)

---

### Pitfall 5: Not Handling Disconnection Gracefully

**What goes wrong:** The debugger assumes the flecs REST API at localhost:27750 is always available. When the target application exits, crashes, or is restarted, HTTP requests fail. Without proper handling, the debugger either crashes, shows stale data indefinitely with no indication, or spams error messages.

**Why it happens:** During development, the target app is always running. Disconnect handling is deferred as "obvious" but the edge cases are subtle: connection refused vs. timeout vs. partial response vs. server returning errors.

**Consequences:**
- Debugger crashes when target app stops
- Stale data displayed with no visual indicator -- user makes decisions based on outdated information
- Rapid reconnection attempts consume CPU / flood logs
- No way to start debugger before the target app (common workflow)

**Prevention:**
- Define explicit connection states: Disconnected, Connecting, Connected, Reconnecting
- Show connection state prominently in the status bar at all times
- When disconnected: show last-known data grayed out with a timestamp, clear indication of "stale"
- Use exponential backoff for reconnection (500ms, 1s, 2s, 4s, cap at 10s)
- Allow starting the debugger before the target app ("waiting for connection..." state)
- Distinguish between "never connected" and "lost connection"
- Detect partial failures: server up but returning errors (e.g., query endpoint fails)

**Detection:** Start the debugger without the target app running. Kill the target app while the debugger is running. Restart the target app. All three scenarios should be handled visually.

**Which phase should address it:** Phase 2 (HTTP client). The connection state machine is part of the networking layer, but the status bar UI should be in Phase 1.

**Confidence:** HIGH -- standard distributed systems concern, applies to any tool polling a remote service.

---

## Moderate Pitfalls

Mistakes that cause delays, tech debt, or degraded user experience.

---

### Pitfall 6: Rendering All Entities Without Virtual Scrolling

**What goes wrong:** The entity tree can contain hundreds or thousands of entities. Rendering all of them into an ncurses window -- even if offscreen -- wastes CPU time and memory. With 5,000 entities, each poll cycle rebuilds and re-renders thousands of lines that the user cannot see.

**Why it happens:** The naive approach is to iterate all entities and call mvwprintw for each one. ncurses windows can be large, but updating thousands of cells per frame is measurably slow, especially with complex formatting (colors, tree lines, indentation).

**Consequences:**
- Sluggish scrolling with large entity counts
- High CPU usage proportional to total entity count (not visible count)
- The debugger becomes the bottleneck, slowing down the application it is supposed to debug (contention on flecs REST server)
- Memory proportional to total entities rather than viewport

**Prevention:**
- Implement virtual scrolling: only render entities visible in the viewport (viewport_height + small buffer)
- Maintain a flat array of "visible" entities (pre-order traversal of expanded tree) and render only the slice [scroll_offset, scroll_offset + viewport_height]
- For the entity tree: compute the visible range first, then render only those entries
- Separate data fetching from rendering -- fetch all data, but render only visible subset
- Consider lazy fetching: only fetch component details for entities the user has selected/expanded

**Detection:** Open a CELS app with 1000+ entities. Measure CPU usage. If it scales linearly with entity count rather than viewport size, virtual scrolling is missing.

**Which phase should address it:** Phase 3-4 (entity tree/tab implementation). The data structures should support virtual scrolling from the start even if the initial rendering is simple.

**Confidence:** HIGH -- universal UI performance pattern.

---

### Pitfall 7: Polling Too Aggressively (Overloading the Target App)

**What goes wrong:** Setting a poll interval of 16ms (60fps) for all 6 tabs causes the debugger to fire dozens of HTTP requests per second against the flecs REST API. The REST server runs inside the target application's process and serializes JSON on each request. This steals CPU time from the actual game/app logic, making the debugger a performance problem.

**Why it happens:** Developers want "real-time" updates and set aggressive polling rates. They do not realize that the flecs REST server is an embedded HTTP server that shares the target application's thread/CPU budget. Each request requires query execution, JSON serialization, and HTTP response construction inside the target process.

**Consequences:**
- Target application frame rate drops noticeably when debugger is connected
- The debugger defeats its own purpose -- you cannot debug performance when the debugger causes performance problems
- Network/JSON parsing overhead on the debugger side compounds the issue

**Prevention:**
- Default poll interval: 500ms-1000ms (1-2 Hz), not 60fps
- Make poll interval configurable via command-line flag or runtime key
- Only poll endpoints relevant to the active tab (e.g., do not poll system data when viewing the entity tab)
- Implement adaptive polling: poll faster when data is changing, slower when stable
- Consider delta/change detection: compare new response with previous and skip re-render if unchanged
- Batch multiple queries into fewer requests where the API supports it

**Detection:** Monitor flecs REST API request rate. If it exceeds 5-10 requests/second total, the interval is too aggressive.

**Which phase should address it:** Phase 2 (HTTP client / polling infrastructure). The polling strategy should be designed alongside the HTTP client.

**Confidence:** HIGH -- the flecs REST server is explicitly described as "minimal implementation... sufficient for development purposes, but should not be used in production environments."

**Sources:**
- [Flecs Remote API docs](https://www.flecs.dev/flecs/md_docs_2FlecsRemoteApi.html)

---

### Pitfall 8: Terminal Resize Crashes or Garbles Layout

**What goes wrong:** The user resizes their terminal and the TUI either crashes (accessing coordinates outside window bounds), renders garbled output (windows overlapping or off-screen), or does nothing (layout ignores the new size).

**Why it happens:** Terminal resize handling in ncurses has multiple moving parts:
1. SIGWINCH signal is delivered
2. ncurses internally calls `resizeterm()` (if using its default handler)
3. getch() returns KEY_RESIZE
4. Application must recreate/resize all windows and recalculate layout
5. LINES and COLS environment variables can override actual terminal size (breaks detection)

Developers handle step 3 but forget steps 4-5, or they handle resize in the signal handler (unsafe) instead of the main loop.

**Consequences:**
- Segfault when writing to coordinates beyond resized window
- Windows drawn at wrong positions after resize
- Status bar disappears or overlaps content
- Tab bar truncated or overflows

**Prevention:**
- Handle KEY_RESIZE from getch() in the main loop, never in the signal handler
- On KEY_RESIZE: call `endwin()` then `refresh()` to reinitialize, then recreate all windows with new dimensions
- Design layout as a function of LINES/COLS: `layout_recalculate(LINES, COLS)` that sets all window dimensions
- Set minimum terminal size requirements and display an error message if too small (e.g., 80x24 minimum)
- Test with very small terminals (e.g., 40x10) to find boundary conditions
- Do NOT set LINES/COLS environment variables
- Pads cannot be auto-resized -- if using pads, resize them manually

**Detection:** Resize terminal rapidly while the debugger is running. Shrink to very small, then expand. Switch tabs during resize.

**Which phase should address it:** Phase 1 (foundation). Layout calculation and resize handling is foundational. Each tab must inherit the resize-aware layout system.

**Confidence:** HIGH -- well-documented ncurses behavior.

**Sources:**
- [resizeterm man page](https://invisible-island.net/ncurses/man/resizeterm.3x.html)
- [resize_term man page](https://linux.die.net/man/3/resize_term)

---

### Pitfall 9: UTF-8/Unicode Mishandling

**What goes wrong:** Box-drawing characters, tree lines (e.g., `+--`, `|`), and Unicode symbols render as garbage or cause misaligned columns. Entity names or component values containing non-ASCII characters corrupt the display.

**Why it happens:** ncurses has two variants: `ncurses` (narrow, single-byte) and `ncursesw` (wide, Unicode-capable). Using the wrong one, or forgetting to call `setlocale()`, or linking against the wrong library causes character rendering failures. The three things that must all align: (1) link against `ncursesw`, (2) include `ncursesw/curses.h`, (3) call `setlocale(LC_ALL, "")` before any I/O or initscr().

**Consequences:**
- Box-drawing characters render as `?` or multi-byte garbage
- Column alignment broken (wide chars occupy 2 cells but code assumes 1)
- Entity names with non-ASCII characters corrupt surrounding text
- Works on developer's machine but fails on others with different locale settings

**Prevention:**
- Link against `ncursesw` (not `ncurses`) in CMakeLists.txt: `find_package(Curses REQUIRED)` may find the wrong one; use `pkg-config ncursesw` or explicitly link `-lncursesw`
- Call `setlocale(LC_ALL, "")` as the FIRST line of main(), before any I/O
- Use `ncursesw/curses.h` or verify that `curses.h` maps to the wide version
- For box-drawing: use ACS_ macros (ACS_ULCORNER, ACS_HLINE, etc.) which work in both narrow and wide mode
- For tree lines: prefer ASCII fallback (`+-- |`) with optional Unicode upgrade when `ncursesw` is detected
- Use `waddnwstr()` / `mvwaddnwstr()` for wide-character output
- Test with `LANG=C` locale to verify graceful degradation

**Detection:** Set `LANG=C` and run the debugger. If box-drawing or special characters break, locale handling is wrong.

**Which phase should address it:** Phase 1 (foundation). The ncurses initialization sequence (`setlocale` -> `initscr`) and library linkage must be correct from the start.

**Confidence:** HIGH -- extremely well-documented pitfall with ncursesw.

**Sources:**
- [Ncursesw and Unicode](http://dillingers.com/blog/2014/08/10/ncursesw-and-unicode/)
- [Arch Linux ncurses Unicode thread](https://bbs.archlinux.org/viewtopic.php?id=183365)

---

### Pitfall 10: JSON Response Changes Between Flecs Versions

**What goes wrong:** The JSON parser is written against a specific flecs REST API response format. When flecs updates (especially major versions like v3 to v4), the JSON structure changes -- different field names, nested object layouts, array formats. The debugger silently shows wrong data or crashes on unexpected fields.

**Why it happens:** The flecs REST API is designed for the flecs explorer web UI, not as a stable public API. The documentation notes it uses a "simpler JSON format" in v4 compared to v3. Field names, nesting, and optional fields can change between versions.

**Consequences:**
- Debugger shows empty data or wrong values after flecs update
- NULL pointer dereference parsing unexpected JSON structure
- Silent data corruption (parsing field X as field Y)
- Difficult to debug because the HTTP response "looks fine" but field semantics changed

**Prevention:**
- Parse JSON defensively: always check if a field exists before accessing it
- Use a JSON library that returns NULL/default for missing fields (cJSON does this well)
- Log the raw JSON response at debug verbosity for troubleshooting
- Version-detect the flecs REST API (check `/world` endpoint for version info)
- Abstract the JSON response parsing behind a "flecs data model" layer so parser changes are isolated
- Write tests that use recorded JSON responses to detect parsing regressions
- Pin expected flecs version in documentation; warn if version mismatch detected

**Detection:** Update flecs in the parent CELS project and run the debugger. If any tab shows wrong or empty data, the parser needs updating.

**Which phase should address it:** Phase 2 (HTTP/JSON layer). The JSON parsing layer should be isolated from the rendering layer so that response format changes require updating only the parser.

**Confidence:** MEDIUM -- flecs v4 release notes confirm format changes, but specific stability guarantees for the REST API are not documented.

**Sources:**
- [Flecs Remote API](https://www.flecs.dev/flecs/md_docs_2FlecsRemoteApi.html)
- [Flecs v4 announcement](https://ajmmertens.medium.com/flecs-v4-0-is-out-58e99e331888)

---

### Pitfall 11: Memory Leaks from JSON Parsing

**What goes wrong:** Each poll cycle allocates a JSON document (parsed from HTTP response). If the document is not freed, or if individual string values are extracted without understanding ownership semantics, memory grows continuously. At 2 polls/second, a 10KB response leaks ~72MB per hour.

**Why it happens:** JSON libraries in C require manual memory management. cJSON uses `cJSON_Delete()` to free a parsed tree; yyjson uses `yyjson_doc_free()`. Developers extract values with `cJSON_GetObjectItem()` (returns internal pointer, no copy) then later free the document, making the extracted pointer dangling. Or they copy strings with `strdup()` and forget to free them.

**Consequences:**
- Steadily growing memory usage (visible in Performance tab, ironically)
- Eventually crashes from out-of-memory
- Dangling pointers if document freed while references still held
- Particularly insidious because it is not visible during short development sessions

**Prevention:**
- Establish a clear ownership pattern: parse JSON -> extract values into owned structs -> free JSON document -> use owned structs until next poll
- Use a "snapshot" pattern: each poll cycle produces a complete data snapshot that owns all its strings; the previous snapshot is freed when the new one is ready
- Never hold pointers into the cJSON/yyjson tree after freeing the document
- Run under Valgrind/AddressSanitizer during development with the debugger polling for 5+ minutes
- If using yyjson: use the immutable document API (`yyjson_doc`) which allocates everything in a single block -- one `yyjson_doc_free()` releases everything

**Detection:** Run the debugger for 10+ minutes. Monitor RSS memory growth. If it grows linearly over time, there is a leak.

**Which phase should address it:** Phase 2 (JSON parsing). The data snapshot pattern and ownership model should be defined when the JSON parser is built.

**Confidence:** HIGH -- universal C memory management concern, especially with polling patterns.

---

### Pitfall 12: Subwindow Deletion Order and Memory

**What goes wrong:** ncurses subwindows share memory with their parent. Deleting the parent window before its subwindows causes undefined behavior. Recreating windows on resize without deleting old ones leaks memory. Using `derwin()`/`subwin()` without understanding the coordinate system differences causes misplaced rendering.

**Why it happens:** ncurses has multiple window creation functions (`newwin`, `subwin`, `derwin`, `newpad`) with subtly different semantics. `subwin` uses screen-relative coordinates; `derwin` uses parent-relative coordinates. The documentation warns that "subwindow functions are flaky, incompletely implemented, and not well tested."

**Consequences:**
- Use-after-free crashes
- Memory leaks on terminal resize (windows recreated without deleting old ones)
- Off-by-one rendering when mixing coordinate systems
- Subtle bugs where touching a subwindow unexpectedly modifies the parent

**Prevention:**
- Prefer `derwin()` over `subwin()` for relative positioning
- Better yet: prefer independent windows (`newwin()`) over subwindows unless you specifically need shared memory
- On resize: delete all windows in reverse order (children first, parents last), then recreate
- Track all created windows in a struct/array so cleanup is reliable
- Call `delwin()` on every window before `endwin()` in shutdown
- Consider panels library (`panel.h`) for managing overlapping windows -- it handles z-order and refresh correctly

**Detection:** Run under Valgrind. Resize the terminal 20 times. If memory grows, windows are leaking.

**Which phase should address it:** Phase 1 (foundation). The window management strategy (how windows are created, tracked, and destroyed) is a foundational decision.

**Confidence:** HIGH -- documented ncurses behavior.

**Sources:**
- [curs_window man page](https://invisible-island.net/ncurses/man/curs_window.3x.html)
- [Curses Windows, Pads, and Panels](http://graysoftinc.com/terminal-tricks/curses-windows-pads-and-panels)

---

### Pitfall 13: macOS ncurses Compatibility

**What goes wrong:** macOS ships an older ncurses (5.7, from 2008) via Apple's SDK. This version lacks features available in Linux ncurses 6.x: extended color support (256+ colors), extended color pairs, some wide character improvements, and various bug fixes. Code that works on Linux fails on macOS.

**Why it happens:** Apple froze their ncurses at 5.7 and does not update it. Developers build on Linux where ncurses 6.x is standard, then discover failures when porting to macOS.

**Consequences:**
- Extended color features fail silently on macOS
- Color pair limit of 256 (vs. 32767 on ncurses 6.x)
- Potential ABI differences if linking against Homebrew ncurses vs. system ncurses
- Build system confusion: `-lncurses` finds system version, Homebrew version requires explicit paths

**Prevention:**
- Target ncurses 5.7 feature set as baseline (conservative approach)
- Limit color pairs to 256 or fewer
- Use `COLOR_PAIR()` macro (limited to 256 pairs) with `A_COLOR` for portability; avoid extended pair functions
- On macOS, detect and recommend Homebrew ncurses if advanced features are needed
- In CMakeLists.txt: handle both system and Homebrew ncurses paths on macOS
- Use `#ifdef NCURSES_VERSION_MAJOR` to conditionally enable features
- Design the color/theme system with a fallback mode: full color (ncurses 6.x), basic color (ncurses 5.7), no color

**Detection:** Build and run on macOS with system ncurses. Test color rendering.

**Which phase should address it:** Phase 1 (foundation, build system) and whichever phase adds color/theme support. Not critical for initial Linux-only development but must be considered in architecture.

**Confidence:** MEDIUM -- based on known macOS ncurses version freeze; specific failure modes need verification on macOS hardware.

---

## Minor Pitfalls

Mistakes that cause annoyance but are fixable without major rework.

---

### Pitfall 14: Forgetting keypad() and Special Keys

**What goes wrong:** Arrow keys, function keys (F1-F12), Home, End, Page Up/Down do not work. Instead of KEY_UP, getch() returns a multi-byte escape sequence that prints garbage or triggers unexpected behavior.

**Why it happens:** By default, ncurses does not translate escape sequences into KEY_ constants. You must call `keypad(stdscr, TRUE)` (and for each window that calls wgetch). Developers forget this and wonder why arrow key navigation does not work.

**Prevention:**
- Call `keypad(win, TRUE)` for every window that will call `wgetch()`
- Typically call it on `stdscr` right after `initscr()`
- If using a dedicated input window, call it on that window too

**Detection:** Press arrow keys. If they do not navigate, `keypad()` was not called.

**Which phase should address it:** Phase 1 (foundation, ncurses initialization).

**Confidence:** HIGH.

---

### Pitfall 15: getch() Consumes KEY_RESIZE Before You Check

**What goes wrong:** With `nodelay()` or `timeout()` set, the main loop calls `getch()` to check for input, but processes the return value only for known keys. KEY_RESIZE is returned as a normal key value but is silently ignored because the switch/if chain does not include it.

**Why it happens:** Developers add keys incrementally (quit, tab switch, scroll) and forget to add KEY_RESIZE to the handler. Unlike other keys, KEY_RESIZE is generated by the system, not the user, so it does not come up during manual testing until someone actually resizes.

**Prevention:**
- Handle KEY_RESIZE in the main input handler from day one
- Structure the input handler with an explicit "unhandled key" path that logs unexpected values during development

**Detection:** Resize the terminal. If layout does not update, KEY_RESIZE is being dropped.

**Which phase should address it:** Phase 1 (foundation, event loop).

**Confidence:** HIGH.

---

### Pitfall 16: Hardcoded Dimensions and Positions

**What goes wrong:** Windows created with fixed dimensions like `newwin(24, 80, 0, 0)` break when the terminal is not exactly 80x24. Tab content overflows or is truncated. Status bar appears in the middle of the screen or off-screen entirely.

**Why it happens:** Developers test with their default terminal size and hardcode values. The layout works perfectly on their machine and breaks everywhere else.

**Prevention:**
- Always derive positions and dimensions from LINES and COLS
- Define layout as proportional: e.g., "tab bar is 1 line, status bar is 1 line, content area is LINES-3"
- Create a layout struct that computes all positions/sizes from terminal dimensions
- Recalculate layout on every KEY_RESIZE

**Detection:** Run in terminals of different sizes (80x24, 120x40, 40x10).

**Which phase should address it:** Phase 1 (foundation, layout system).

**Confidence:** HIGH.

---

### Pitfall 17: Exposing Raw JSON Error Messages to Users

**What goes wrong:** When the flecs REST API returns an error (invalid query, entity not found, server error), the raw JSON error string or HTTP status code is dumped into the UI. Users see `{"error": "invalid query expression 'foo'"}` instead of a human-readable message.

**Why it happens:** Developers focus on the happy path. Error responses from the REST API are parsed and displayed without formatting or translation.

**Prevention:**
- Parse error responses into a structured error type
- Display human-readable error messages in the status bar or a dedicated error area
- Log raw error details at debug verbosity for troubleshooting
- Map common errors to actionable messages (e.g., "Entity not found" rather than the raw JSON)

**Detection:** Send a malformed query or request a non-existent entity. Check what the user sees.

**Which phase should address it:** Phase 2 (HTTP/JSON layer) for error parsing, Phase 3+ (tabs) for error display.

**Confidence:** MEDIUM.

---

### Pitfall 18: Not Handling Empty/Null Component Values

**What goes wrong:** The flecs REST API returns component data that may be null, empty, or contain unexpected types. A component might be a tag (no value), have default-initialized fields (all zeros), or the `values` parameter might not have been included in the request. The parser crashes or displays "null" strings throughout the UI.

**Why it happens:** During development, test entities always have well-formed component data. Edge cases (tags vs. components, default values, missing fields) only appear with real-world data.

**Prevention:**
- Distinguish between tags (no value), components with values, and components with unknown values
- Handle null/missing JSON fields at the parser level, not the rendering level
- Display tags differently from components (e.g., just the name, no value section)
- Show "(default)" or "(unset)" for zero-initialized or missing values rather than raw "0" or "null"

**Detection:** Add a tag-only component to a test entity. View it in the debugger.

**Which phase should address it:** Phase 2 (JSON parsing) and Phase 3-4 (entity/component tabs).

**Confidence:** MEDIUM -- depends on specific flecs REST API behavior.

---

## Phase-Specific Warnings

| Phase Topic | Likely Pitfall | Mitigation |
|-------------|---------------|------------|
| Foundation / Event Loop | Blocking HTTP freezes UI (P1) | Use curl_multi or separate thread + queue |
| Foundation / ncurses Init | Terminal corruption on crash (P4) | Signal handlers + atexit from day one |
| Foundation / ncurses Init | Unicode broken (P9) | setlocale + ncursesw linkage + ACS macros |
| Foundation / Layout | Resize crashes (P8) | Layout as function of LINES/COLS, KEY_RESIZE handler |
| Foundation / Window Mgmt | Subwindow leaks on resize (P12) | Track all windows, delete in order, or use newwin() |
| Foundation / Rendering | Flicker from wrefresh (P2) | wnoutrefresh + single doupdate per frame |
| HTTP Client | Blocking requests (P1) | curl_multi_perform + curl_multi_poll |
| HTTP Client | Overloading target app (P7) | Conservative poll interval, per-tab polling |
| HTTP Client | No reconnection logic (P5) | State machine: disconnected/connecting/connected/reconnecting |
| JSON Parsing | Memory leaks (P11) | Snapshot pattern: parse -> copy -> free |
| JSON Parsing | Flecs version breaks parser (P10) | Defensive parsing, abstraction layer |
| JSON Parsing | Null/empty values crash (P18) | Null-safe accessors, tag vs component distinction |
| Entity Tree | Rendering thousands of entities (P6) | Virtual scrolling from the start |
| Tabs / Rendering | Hardcoded dimensions (P16) | Proportional layout system |
| Cross-platform | macOS ncurses differences (P13) | Target ncurses 5.7 baseline, limit color pairs |

---

## Architecture-Level Recommendation

The pitfalls above cluster into three architectural decisions that must be made in Phase 1:

**1. Event Loop Model:**
Choose between (a) single-threaded with `curl_multi` + `poll()` or (b) network thread + thread-safe queue + UI thread. Option (a) is simpler and avoids all threading pitfalls but requires more careful integration of curl_multi with ncurses input handling. Option (b) is more conventional but requires rigorous discipline about ncurses thread exclusivity.

**Recommendation:** Single-threaded with `curl_multi`. The polling interval is 500ms+, so the complexity of threading is not justified. Use `timeout(50)` on getch for 50ms input polling, then check `curl_multi` for pending responses.

**2. Rendering Model:**
Every render pass must follow: clear dirty windows -> update content -> `wnoutrefresh()` all -> `doupdate()` once. No exceptions.

**3. Data Ownership Model:**
Each poll cycle produces a "snapshot" struct that owns all data (copies of strings from JSON). The previous snapshot is freed when the new one is complete. Rendering reads from the current snapshot. No pointers into JSON parse trees survive past the parse function.

---

## Sources

### ncurses
- [NCURSES Programming HOWTO](https://tldp.org/HOWTO/NCURSES-Programming-HOWTO/)
- [Writing Programs with NCURSES](https://invisible-island.net/ncurses/ncurses-intro.html)
- [curs_threads man page](https://man7.org/linux/man-pages/man3/curs_threads.3x.html)
- [resizeterm man page](https://invisible-island.net/ncurses/man/resizeterm.3x.html)
- [curs_window man page](https://invisible-island.net/ncurses/man/curs_window.3x.html)
- [Curses Windows, Pads, and Panels](http://graysoftinc.com/terminal-tricks/curses-windows-pads-and-panels)
- [Event Loops and NCurses](https://linuxjedi.co.uk/event-loops-and-ncurses/)
- [Ncursesw and Unicode](http://dillingers.com/blog/2014/08/10/ncursesw-and-unicode/)

### libcurl
- [curl_easy_perform](https://curl.se/libcurl/c/curl_easy_perform.html)
- [libcurl multi interface](https://curl.se/libcurl/c/libcurl-multi.html)
- [curl_multi_poll](https://curl.se/libcurl/c/curl_multi_poll.html)
- [curl thread safety](https://curl.se/libcurl/c/threadsafe.html)
- [CURLOPT_TIMEOUT](https://curl.se/libcurl/c/CURLOPT_TIMEOUT.html)
- [CURLOPT_CONNECTTIMEOUT](https://curl.se/libcurl/c/CURLOPT_CONNECTTIMEOUT.html)

### Flecs
- [Flecs Remote API](https://www.flecs.dev/flecs/md_docs_2FlecsRemoteApi.html)
- [Flecs v4 announcement](https://ajmmertens.medium.com/flecs-v4-0-is-out-58e99e331888)

### JSON
- [cJSON GitHub](https://github.com/DaveGamble/cJSON)
- [yyjson GitHub](https://github.com/ibireme/yyjson)
