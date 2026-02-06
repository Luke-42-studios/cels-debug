# CELS Debug TUI - Terminal-based ECS inspector
cmake_minimum_required(VERSION 3.21)
project(cels-debug VERSION 0.1.0 LANGUAGES C)

# === Dependencies ===
include(FetchContent)

# yyjson - Fast JSON parser (FetchContent, consistent with CELS flecs pattern)
FetchContent_Declare(
    yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG        0.12.0
)
FetchContent_MakeAvailable(yyjson)

# ncursesw - TUI rendering (system package, wide-char for UTF-8)
set(CURSES_NEED_WIDE TRUE)
set(CURSES_NEED_NCURSES TRUE)
find_package(Curses REQUIRED)

# libcurl - HTTP client (system package)
find_package(CURL REQUIRED)

# === Executable ===
add_executable(cels-debug
    src/main.c
    src/http_client.c
    src/json_parser.c
    src/data_model.c
    src/tui.c
    src/tab_system.c
    src/scroll.c
    src/split_panel.c
    src/json_render.c
    src/tree_view.c
    src/tabs/tab_placeholder.c
    src/tabs/tab_overview.c
    src/tabs/tab_ecs.c
)

set_target_properties(cels-debug PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

target_include_directories(cels-debug PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CURSES_INCLUDE_DIRS}
)

target_link_libraries(cels-debug PRIVATE
    ${CURSES_LIBRARIES}
    CURL::libcurl
    yyjson
)
