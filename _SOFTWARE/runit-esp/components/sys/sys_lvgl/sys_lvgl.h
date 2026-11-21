#pragma once

#include <stdint.h>
#include "errors.h"
#include "sys_device.h"
#include "sys_event.h"
#include "sys_lvgl_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The LVGL adapter (SYS_LVGL.MD): LVGL, its task and its lock, the draw buffers and LVGL's one pool, on one display
 * (contract DISPLAY) and an optional touch (contract TOUCH). Not a device: its commands are under MODULE_ID_LVGL
 * (sys_lvgl_router.h). The screen's content is the upper layers' (sys_screen); they touch lv_* only under
 * sys_lvgl_lock().
 */

//#ref-enum @alias Screen adapter state
typedef enum sys_lvgl_state_e {
  SYS_LVGL_STATE_EMPTY = 0,    //@alias Empty @description No display: LVGL is not running and holds no memory
  SYS_LVGL_STATE_RUNNING = 1,  //@alias Running @description Draws and reads the touch
  SYS_LVGL_STATE_STOPPED = 2,  //@alias Stopped @description Stopped by STOP: the screen and its objects are kept
  SYS_LVGL_STATE_PAUSED = 3,   //@alias Paused @description The display is suspended: drawing resumes with it
} sys_lvgl_state_e;

/**
 * @brief The events of the adapter (module MODULE_ID_LVGL, lane SYS), source and channel 0: X(P, NAME, code, flags,
 * { payload }) (sys_event.h). Event codes are wire format: never renumber.
 *   RESET:    the adapter is about to be reset (RESET, or the display or the touch was REMOVEd): the objects of LVGL
 *             are still there, the next thing is lv_display_delete. Published inline under the LVGL lock: a listener
 *             (sys_screen) drops its widgets and timers, calls no one that waits, and takes no other lock.
 *   STATE:    the adapter's state changed (sys_lvgl_state_e); published deferred, in the lane's task
 *   ROTATION: ROTATION worked (sys_display_orientation_e); published deferred
 */
#define LVGL_EVENTS(X, P)                                       \
  X(P, RESET,    0x01, 0,                {})                    \
  X(P, STATE,    0x02, SYS_EVENT_NOTIFY, { uint8_t state; })    \
  X(P, ROTATION, 0x03, SYS_EVENT_NOTIFY, { uint8_t rotation; })

SYS_EVENTS_DECLARE(LVGL, MODULE_ID_LVGL, SYS)

#define SYS_LVGL_HEAP_MIN    32768u    // the smallest pool INSTALL takes (B): lv_conf.h's LV_MEM_SIZE
#define SYS_LVGL_HEAP_MAX    4194304u  // the largest: LV_MEM_SIZE + LV_MEM_POOL_EXPAND_SIZE
#define SYS_LVGL_REFRESH_MIN 1u        // the slowest refresh INSTALL takes (Hz)
#define SYS_LVGL_REFRESH_MAX 60u       // the fastest (Hz)

/** @brief INSTALL args and answer */
typedef struct __attribute__((packed)) {
  uint8_t display;      /**< Display device (contract DISPLAY) */
  uint8_t touch;        /**< Touch device (contract TOUCH), or SYS_DEVICE_ID_NONE */
  uint32_t heap_bytes;  /**< LVGL's one pool (objects, styles, caches); 0: CONFIG_SYS_LVGL_HEAP_DEFAULT */
  uint8_t refresh_hz;   /**< Frames a second at most, the touch read as often; 0: SYS_LVGL_REFRESH_HZ (lv_conf.h) */
} sys_lvgl_install_t;

/** @brief Screen rotation (degrees clockwise, matches LV_DISPLAY_ROTATION_*). */
//#ref-enum @alias Screen rotation
typedef enum sys_lvgl_rotation_e {
  SYS_LVGL_ROTATION_0 = 0,    //@alias 0 deg @description Normal orientation (0 degrees)
  SYS_LVGL_ROTATION_90 = 1,   //@alias 90 deg @description Rotated 90 degrees clockwise
  SYS_LVGL_ROTATION_180 = 2,  //@alias 180 deg @description Rotated 180 degrees
  SYS_LVGL_ROTATION_270 = 3,  //@alias 270 deg @description Rotated 270 degrees clockwise
} sys_lvgl_rotation_e;

/** @brief ROTATION args and answer */
typedef struct __attribute__((packed)) {
  uint8_t rotation;  /**< sys_lvgl_rotation_e: 0, 90, 180, 270 deg */
} sys_lvgl_rotation_t;

/** @brief What STATUS answers */
typedef struct __attribute__((packed)) {
  uint8_t state;          /**< sys_lvgl_state_e */
  uint8_t rotation;       /**< sys_display_orientation_e */
  uint8_t buffers;        /**< Draw buffers: 2, or 1 when RAM was short; 0 when empty */
  uint16_t width;         /**< Pixels, with the rotation */
  uint16_t height;        /**< Pixels, with the rotation */
  uint32_t buffer_bytes;  /**< One draw buffer */
  uint32_t heap_budget;   /**< LVGL's pool, as TLSF counts it (INSTALL's less its own bookkeeping); 0 when empty */
  uint32_t heap_used;     /**< Bytes of the pool in use */
  uint32_t heap_peak;     /**< The most in use since INSTALL */
  uint32_t flushes;       /**< FLUSHes the display accepted since INSTALL */
  uint32_t flush_errors;  /**< FLUSHes that failed or timed out since INSTALL */
} sys_lvgl_status_t;

/**
 * @brief The LVGL lock (a recursive mutex; LVGL has no OS of its own, LV_OS_NONE): every lv_* call outside the LVGL
 * task is made under it, and only while installed (STATUS). Never under the registry lock (inside a device op or the
 * state listener): the LVGL task holds this one across FLUSH, a deadlock. Held by the LVGL task at most one frame.
 */
void sys_lvgl_lock(void);

/** @brief Releases the LVGL lock taken by sys_lvgl_lock(). */
void sys_lvgl_unlock(void);

/**
 * @brief INSTALL: the display and the touch, LVGL on its pool of heap_bytes, the draw buffers; then the adapter runs.
 * A REMOVE of the display or the touch while installed resets the adapter (the registry's REMOVED event).
 * @param[in] install The display, the touch and the pool size
 * @return ERR_LVGL_INSTALLED when installed (RESET first), ERR_LVGL_NOT_DISPLAY / ERR_LVGL_NOT_TOUCH around the
 * device's error, ERR_LVGL_FORMAT, ERR_LVGL_FLUSH_MAX, ERR_LVGL_NO_MEMORY, ERR_LVGL_HEAP, ERR_LVGL_REFRESH; a failure
 * leaves it empty
 */
MUST_USE err_h sys_lvgl_install(const sys_lvgl_install_t* install);

/**
 * @brief What is installed: SYS_DEVICE_ID_NONE, heap_bytes and refresh_hz 0 when empty.
 * @param[out] out The display, the touch, the pool size and the refresh rate
 * @return The error of the read
 */
MUST_USE err_h sys_lvgl_get_install(sys_lvgl_install_t* out);

/**
 * @brief RESET: waits for the last FLUSH, publishes LVGL_EV_RESET, deletes the touch and the display, stops LVGL
 * (lv_deinit), frees its pool and the buffers. Empty: nothing to do.
 * @return The error of deleting the devices
 */
MUST_USE err_h sys_lvgl_reset(void);

/**
 * @brief START: draws and reads the touch again, the whole screen redrawn.
 * @return ERR_LVGL_NOT_INSTALLED when empty
 */
MUST_USE err_h sys_lvgl_start(void);

/**
 * @brief STOP: waits for the last FLUSH; no drawing, no touch; the objects stay.
 * @return ERR_LVGL_NOT_INSTALLED when empty
 */
MUST_USE err_h sys_lvgl_stop(void);

/**
 * @brief ROTATION: waits for the last FLUSH, turns the panel (DISPLAY ORIENTATION), then LVGL (lv_display_set_rotation,
 * which redraws the screen). While the display is suspended (PAUSED) only LVGL turns: the panel takes the rotation when
 * it resumes.
 * @param[in] rotation sys_display_orientation_e
 * @return ERR_LVGL_NOT_INSTALLED when empty, ERR_DEV_RANGE past 270 deg, or the display's error
 */
MUST_USE err_h sys_lvgl_set_rotation(uint8_t rotation);

/**
 * @brief Reads the rotation
 * @param[out] out sys_display_orientation_e
 * @return ERR_LVGL_NOT_INSTALLED when empty, or the error of the read
 */
MUST_USE err_h sys_lvgl_get_rotation(uint8_t* out);

/**
 * @brief Reads the adapter's state, buffers, pool and flush counters
 * @param[out] out The status
 * @return The error of the read
 */
MUST_USE err_h sys_lvgl_get_status(sys_lvgl_status_t* out);

/**
 * @brief Before anything that makes LVGL allocate (a widget, a longer text): whether `need` bytes fit in LVGL's pool
 * and leave CONFIG_SYS_LVGL_POOL_RESERVE free, for what LVGL allocates by itself while it draws. LVGL does not survive
 * a full pool (SYS_LVGL.MD "The pool"): the layer that makes widgets calls this first and refuses with its error.
 * @param[in] need Bytes the caller is about to make LVGL allocate
 * @return ERR_LVGL_NOT_INSTALLED when empty, ERR_LVGL_POOL_FULL{free, need, reserve}
 */
MUST_USE err_h sys_lvgl_pool_check(uint32_t need);

#ifdef __cplusplus
}
#endif
