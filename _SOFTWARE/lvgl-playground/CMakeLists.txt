cmake_minimum_required(VERSION 3.28)
project(lvgl_playground C)

set(CMAKE_C_STANDARD 11)

set(LV_BUILD_EXAMPLES OFF CACHE BOOL "")
set(LV_BUILD_DEMOS OFF CACHE BOOL "")

include(FetchContent)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.3.0
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(lvgl)

# LVGL reads its configuration from our lv_conf.h, not from its own tree.
target_compile_definitions(lvgl PUBLIC LV_CONF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h")

option(PLAYGROUND_WINDOW "Show the mocked panel in an SDL2 window (mouse = touch)" ON)

add_executable(playground
    main.c
    mock/mock_panel.c
    mock/mock_touch.c
    contracts/contracts_check.c)
target_include_directories(playground PRIVATE mock contracts contracts/host_stub)
target_link_libraries(playground PRIVATE lvgl)

if(PLAYGROUND_WINDOW)
    set(SDL_SHARED OFF CACHE BOOL "")
    set(SDL_STATIC ON CACHE BOOL "")
    set(SDL_TEST OFF CACHE BOOL "")
    FetchContent_Declare(SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-2.32.10
        GIT_SHALLOW    TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(SDL2)
    target_sources(playground PRIVATE mock/mock_window.c)
    target_compile_definitions(playground PRIVATE HAVE_WINDOW)
    target_link_libraries(playground PRIVATE SDL2::SDL2-static)
endif()

# `cmake --build build --target window` opens the window (needs PLAYGROUND_WINDOW)
if(PLAYGROUND_WINDOW)
    add_custom_target(window COMMAND playground --window 2 DEPENDS playground USES_TERMINAL)
endif()

# `cmake --build build --target run` renders the scenario into out/*.bmp
add_custom_target(run
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/out
    COMMAND playground ${CMAKE_CURRENT_SOURCE_DIR}/out
    DEPENDS playground
    USES_TERMINAL)
