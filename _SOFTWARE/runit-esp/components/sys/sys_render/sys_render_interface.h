/**
 * @file
 * @brief What the render needs of the engine that draws (sys_render_driver_t): the module knows
 *        the engine only through this table, so it can be swapped, and in tests faked. The
 *        implementation of the table is not tested in TDD.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "sys_display/sys_display_contract.h" /* display_window_t */

/**
 * @brief Sends one rendered window to the panel; returns once queued (the panel's done comes
 * later).
 * @param ctx Passed from sys_render_display_cfg_t
 * @param w Where the pixels go
 * @param px The pixels of the window
 * @param len Bytes of the window
 */
typedef void (*sys_render_flush_f)(void *ctx, const display_window_t *w, const void *px,
                                   uint32_t len);

/**
 * @brief Waits until the last flush is done; the render calls it before it reuses a buffer.
 * @param ctx Passed from sys_render_display_cfg_t
 */
typedef void (*sys_render_wait_f)(void *ctx);

/** @brief What display_create takes */
typedef struct {
    uint16_t w;               /**< Native width of the panel, px */
    uint16_t h;               /**< Native height of the panel, px */
    uint8_t format;           /**< sys_display_format_e */
    void *buf1;               /**< Render buffer */
    void *buf2;               /**< Second render buffer */
    uint32_t buf_bytes;       /**< Size of each buffer */
    uint32_t refresh_ms;      /**< Longest wait between two checks for something to draw */
    sys_render_flush_f flush; /**< Sends a window to the panel */
    sys_render_wait_f wait;   /**< Waits for the end of the last flush */
    void *ctx;                /**< Passed to flush and wait */
} sys_render_display_cfg_t;

/** @brief The engine that draws: one table of what the render asks of it */
typedef struct sys_render_driver_s {
    /** @brief Gives the engine its pool (all of it); before init */
    void (*set_pool)(void *pool, uint32_t pool_bytes);
    /** @brief Starts the engine on that pool, clock = tick_ms, logs; false when the pool does not
     * take */
    bool (*init)(uint32_t (*tick_ms)(void));
    /** @brief Stops the engine and frees its internals */
    void (*deinit)(void);
    /** @brief One render round; returns the ms to the next one */
    uint32_t (*timer_handler)(void);
    /** @brief Makes the display; NULL when out of memory */
    void *(*display_create)(const sys_render_display_cfg_t *cfg);
    /** @brief Deletes the display made by display_create */
    void (*display_delete)(void *disp);
    /** @brief Turns the picture of the display: 0..3, as in the display contract */
    void (*display_set_rotation)(void *disp, uint8_t rotation);
    /** @brief The last window was sent: the render may go on */
    void (*display_flush_ready)(void *disp);
    /** @brief Marks the whole screen for a redraw: on start, resume, reset and flush timeout */
    void (*invalidate_screen)(void);
    /** @brief The largest free block of the object pool, in bytes */
    uint32_t (*pool_free_biggest)(void);
} sys_render_driver_t;

/**
 * @brief The render engine the module runs on: one instance, defined by the implementation of the
 * library (lvgl_impl) and, in tests, by the test. Never null.
 */
extern const sys_render_driver_t sys_render_driver;
