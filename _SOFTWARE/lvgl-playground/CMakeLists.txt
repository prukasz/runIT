cmake_minimum_required(VERSION 3.28)
project(runit_screen_sim C)

# The screen simulator (README.md): the firmware's sys_lvgl, sys_device, sys_display, sys_touch, router, cmd and errors,
# built from runit-esp/components as they are, on the host stand-ins of runit-esp/test_host (FreeRTOS as threads), with
# the display driver (driver/) on a simulated SPI bus and ILI9341 (mock/), a simulated touch and GPIO (sim/).

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)  # typeof, statement expressions (the errors' macros)

set(ESP ${CMAKE_CURRENT_SOURCE_DIR}/../runit-esp)
set(COMP ${ESP}/components)
set(HOST ${ESP}/test_host)

find_package(Threads REQUIRED)

# LVGL as on the board: its configuration is sys_lvgl's lv_conf.h.
set(LV_BUILD_EXAMPLES OFF CACHE BOOL "")
set(LV_BUILD_DEMOS OFF CACHE BOOL "")
set(LV_BUILD_CONF_PATH ${COMP}/sys/sys_lvgl/lv_conf.h CACHE PATH "" FORCE)
include(FetchContent)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.3.0
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(lvgl)
target_compile_definitions(lvgl PUBLIC LV_KCONFIG_IGNORE)
target_include_directories(lvgl PUBLIC ${COMP}/sys/sys_lvgl)

option(PLAYGROUND_WINDOW "Show the glass in an SDL2 window (mouse = touch)" ON)

add_executable(playground
    main.c
    driver/lcd_spi.c
    sim/sim_gpio.c
    sim/sim_touch.c
    sim/sim_demo.c
    sim/sim_scenario.c
    mock/mock_spi.c
    mock/mock_ili9341.c
    mock/mock_panel.c
    mock/mock_touch.c
    # the host stand-ins
    ${HOST}/src/host_mocks.c
    ${HOST}/src/host_freertos.c
    # the firmware
    ${COMP}/core/errors/errors.c
    ${COMP}/core/errors/err_pool.c
    ${COMP}/core/errors/err_tags.c
    ${COMP}/core/errors/err_log.c
    ${COMP}/core/errors/err_wire.c
    ${COMP}/core/cmd/cmd.c
    ${COMP}/core/router/router.c
    ${COMP}/core/router/router_dispatch.c
    ${COMP}/sys/sys_device/registry/sys_device.c
    ${COMP}/sys/sys_device/policy/sys_device_policy.c
    ${COMP}/sys/sys_device/router/sys_device_router.c
    ${COMP}/sys/sys_io/sys_io.c
    ${COMP}/sys/sys_display/sys_display.c
    ${COMP}/sys/sys_touch/sys_touch.c
    ${COMP}/sys/sys_lvgl/sys_lvgl.c
    ${COMP}/sys/sys_lvgl/router/sys_lvgl_router.c)
target_include_directories(playground PRIVATE
    driver sim mock
    ${HOST}/include
    ${COMP}/core/errors ${COMP}/core/errors/host
    ${COMP}/core/cmd ${COMP}/core/cmd/errors
    ${COMP}/core/router ${COMP}/core/router/errors ${COMP}/core/router/host
    ${COMP}/sys/sys_device/registry ${COMP}/sys/sys_device/errors ${COMP}/sys/sys_device/policy
    ${COMP}/sys/sys_device/router
    ${COMP}/sys/sys_io ${COMP}/sys/sys_io/errors
    ${COMP}/sys/sys_display ${COMP}/sys/sys_display/errors
    ${COMP}/sys/sys_touch ${COMP}/sys/sys_touch/errors
    ${COMP}/sys/sys_lvgl ${COMP}/sys/sys_lvgl/errors ${COMP}/sys/sys_lvgl/router)
target_link_libraries(playground PRIVATE lvgl Threads::Threads)

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

# `cmake --build build --target window` runs the demo scenario in the window (needs PLAYGROUND_WINDOW)
if(PLAYGROUND_WINDOW)
    add_custom_target(window COMMAND playground --window 2 ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/demo.txt
                      DEPENDS playground USES_TERMINAL)
endif()

# `cmake --build build --target run` runs the walk-through without a window, screenshots into out/
add_custom_target(run
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/out
    COMMAND playground --out ${CMAKE_CURRENT_SOURCE_DIR}/out ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/walkthrough.txt
    DEPENDS playground
    USES_TERMINAL)
