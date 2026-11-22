/**
 * @file
 * @brief The render adapter: the engine's task and lock, its one pool and the draw buffers, on one
 *        display (contract DISPLAY). Not a device: its remote commands are under MODULE_ID_RENDER
 *        (router/sys_render_router.h). What is on the screen is the upper layers': they call the
 * engine only under sys_render_lock().
 */
#pragma once
#include <stdint.h>
#include "errors.h"
#include "sys_event.h"
#include "sys_render_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief State of the adapter */
typedef enum sys_render_state_e {
    SYS_RENDER_STATE_EMPTY = 0, /**< Not installed: the engine is not running and holds no memory */
    SYS_RENDER_STATE_RUNNING = 1, /**< Draws */
    SYS_RENDER_STATE_STOPPED = 2, /**< Stopped by stop (or just installed): the screen is kept */
    SYS_RENDER_STATE_PAUSED = 3,  /**< The display is suspended: drawing resumes with it */
} sys_render_state_e;

/**
 * @brief The events of the adapter (module MODULE_ID_RENDER, lane SYS), source and channel 0:
 * X(P, NAME, code, flags, { payload }) (sys_event.h). Event codes are wire format: never renumber.
 *   RESET:            the adapter is about to be reset: its objects are still there. Published
 *                     inline under the render lock: a listener drops what it made in the engine and
 *                     returns.
 *   STATE_CHANGED:    the state changed (sys_render_state_e); published deferred
 *   ROTATION_CHANGED: set_rotation worked (sys_display_orientation_e); published deferred
 */
#define RENDER_EVENTS(X, P)                                                                        \
    X(P, RESET, 0x01, 0, {})                                                                       \
    X(P, STATE_CHANGED, 0x02, SYS_EVENT_NOTIFY, { uint8_t state; })                                \
    X(P, ROTATION_CHANGED, 0x03, SYS_EVENT_NOTIFY, { uint8_t rotation; })

SYS_EVENTS_DECLARE(RENDER, MODULE_ID_RENDER, SYS)

/** @brief install args and answer */
typedef struct {
    uint8_t display;     /**< Display device (contract DISPLAY) */
    uint8_t touch;       /**< Touch device (contract TOUCH) */
    uint32_t heap_bytes; /**< The engine's one pool (objects, styles, caches):
                              SYS_RENDER_HEAP_BYTES_MIN .. _MAX */
    uint8_t refresh_hz;  /**< Frames a second at most: SYS_RENDER_REFRESH_HZ_MIN .. _MAX */
} __attribute__((packed)) sys_render_install_t;

/** @brief set_rotation args and answer */
typedef struct {
    uint8_t rotation; /**< sys_display_orientation_e: 0..3 (0, 90, 180, 270 deg) */
} __attribute__((packed)) sys_render_rotation_t;

/**
 * @brief Takes the render lock (a recursive mutex), waiting as long as it takes. Every call into
 * the engine outside the render task is made under it. Never under the registry's lock, never from
 * an ISR.
 */
void sys_render_lock(void);

/** @brief Releases the render lock taken by sys_render_lock(). */
void sys_render_unlock(void);

/**
 * @brief Installs the render on a display: reads its GEOMETRY and ORIENTATION, takes the pool and
 * the draw buffers, starts the engine and listens to the display's events. The state becomes
 * STOPPED. A failure frees everything it made and leaves the adapter EMPTY.
 * @param[in] install The display, the pool size and the refresh rate. Checked in this order: not
 * NULL, not installed, refresh_hz and heap_bytes inside their ranges (both ends are allowed),
 * then the display
 * @return ERR_NULL_PTR, ERR_RENDER_ALREADY_INSTALLED, ERR_RENDER_REFRESH_HZ_OUT_OF_RANGE,
 * ERR_RENDER_HEAP_BYTES_OUT_OF_RANGE, the display's own error (its GEOMETRY or ORIENTATION,
 * as it is), ERR_RENDER_PIXEL_FORMAT_UNSUPPORTED, ERR_RENDER_ROW_EXCEEDS_FLUSH_MAX,
 * ERR_RENDER_ALLOC_FAILED, or the error of the event subscription
 */
MUST_USE err_h sys_render_install(const sys_render_install_t *install);

/**
 * @brief What is installed: SYS_DEVICE_ID_NONE, heap_bytes and refresh_hz 0 when empty.
 * @param[out] out The display, the pool size and the refresh rate
 * @return ERR_NULL_PTR, or NULL
 */
MUST_USE err_h sys_render_get_install(sys_render_install_t *out);

/**
 * @brief Waits for the last FLUSH, publishes RENDER_EV_RESET, deletes the display, stops the
 * engine, frees the pool and the buffers and stops listening to the display. Empty: nothing to do.
 * @return NULL
 */
MUST_USE err_h sys_render_reset(void);

/**
 * @brief Starts drawing, the whole screen redrawn. A suspended display leaves the adapter PAUSED.
 * Starting a running adapter does nothing.
 * @return ERR_RENDER_NOT_INSTALLED when empty, or the error of reading the display's INFO (the
 * state is then unchanged)
 */
MUST_USE err_h sys_render_start(void);

/**
 * @brief Stops drawing after the last FLUSH; the objects stay. Stopping a stopped adapter does
 * nothing.
 * @return ERR_RENDER_NOT_INSTALLED when empty
 */
MUST_USE err_h sys_render_stop(void);

/**
 * @brief Turns the panel (ORIENTATION) and then the engine's display. While the display is
 * suspended (PAUSED) only the engine turns: the panel takes the rotation when it resumes.
 * @param[in] rotation sys_display_orientation_e: 0..3 (both ends are allowed)
 * @return ERR_RENDER_NOT_INSTALLED when empty (checked before the range), ERR_RANGE past 3, or
 * the display's error
 */
MUST_USE err_h sys_render_set_rotation(uint8_t rotation);

/**
 * @brief Reads the rotation.
 * @param[out] out sys_display_orientation_e
 * @return ERR_NULL_PTR, ERR_RENDER_NOT_INSTALLED when empty, or NULL
 */
MUST_USE err_h sys_render_get_rotation(uint8_t *out);

/**
 * @brief Reads the state of the adapter, in any state.
 * @param[out] out sys_render_state_e
 * @return ERR_NULL_PTR, or NULL
 */
MUST_USE err_h sys_render_get_state(uint8_t *out);

/**
 * @brief Before anything that makes the engine allocate: whether `need` bytes fit in the engine's
 * pool (its largest free block) and leave SYS_RENDER_POOL_RESERVE_BYTES free.
 * @param[in] need Bytes the caller is about to make the engine allocate
 * @return ERR_RENDER_NOT_INSTALLED when empty, ERR_RENDER_POOL_BELOW_RESERVE{free, need, reserve}
 */
MUST_USE err_h sys_render_pool_check(uint32_t need);

#ifdef __cplusplus
}
#endif
