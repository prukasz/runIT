#pragma once

#include <stdint.h>
#include "errors.h"
#include "sys_device.h"
#include "sys_lvgl_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The LVGL adapter (SYS_LVGL.MD): LVGL, its task and its lock, the draw buffers and LVGL's one pool, on one display
 * (contract DISPLAY) and an optional touch (contract TOUCH). Not a device: its commands are under ROUTER_KEY_LVGL
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

#define SYS_LVGL_HEAP_MIN 32768u    // the smallest pool INSTALL takes (B): lv_conf.h's LV_MEM_SIZE
#define SYS_LVGL_HEAP_MAX 4194304u  // the largest: LV_MEM_SIZE + LV_MEM_POOL_EXPAND_SIZE

/** @brief INSTALL args and answer */
typedef struct __attribute__((packed)) {
  uint16_t display;     /**< Display device (contract DISPLAY) */
  uint16_t touch;       /**< Touch device (contract TOUCH), or SYS_DEVICE_ID_NONE */
  uint32_t heap_bytes;  /**< LVGL's one pool (objects, styles, caches); 0: CONFIG_SYS_LVGL_HEAP_DEFAULT */
} sys_lvgl_install_t;

/** @brief ROTATION args and answer */
typedef struct __attribute__((packed)) {
  uint8_t rotation;  /**< sys_display_orientation_e: 0, 90, 180, 270 deg */
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
 * @brief Boot step: the LVGL task, idle. LVGL itself runs from INSTALL (lv_init on its pool) to RESET (lv_deinit).
 */
MUST_USE err_h sys_lvgl_init(void);

/**
 * @brief The LVGL lock (a recursive mutex; LVGL has no OS of its own, LV_OS_NONE): every lv_* call outside the LVGL
 * task is made under it, and only while installed (STATUS). Never under the registry lock (inside a device op or the
 * state listener): the LVGL task holds this one across FLUSH, a deadlock. Held by the LVGL task at most one frame.
 */
void sys_lvgl_lock(void);
void sys_lvgl_unlock(void);

/**
 * @brief INSTALL: the display and the touch, LVGL on its pool of heap_bytes, the draw buffers; then the adapter runs.
 * @return ERR_LVGL_INSTALLED when installed (RESET first), ERR_LVGL_NOT_DISPLAY / ERR_LVGL_NOT_TOUCH around the
 * device's error, ERR_LVGL_FORMAT, ERR_LVGL_FLUSH_MAX, ERR_LVGL_NO_MEMORY, ERR_LVGL_HEAP; a failure leaves it empty
 */
MUST_USE err_h sys_lvgl_install(const sys_lvgl_install_t* install);

/** @brief What is installed: SYS_DEVICE_ID_NONE and heap_bytes 0 when empty. */
MUST_USE err_h sys_lvgl_get_install(sys_lvgl_install_t* out);

/**
 * @brief RESET: waits for the last FLUSH, calls the reset hook, deletes the touch and the display, stops LVGL
 * (lv_deinit) and frees its pool and the buffers. Empty: nothing to do.
 */
MUST_USE err_h sys_lvgl_reset(void);

/** @brief START: draws and reads the touch again, the whole screen redrawn. ERR_LVGL_NOT_INSTALLED when empty. */
MUST_USE err_h sys_lvgl_start(void);

/** @brief STOP: waits for the last FLUSH; no drawing, no touch; the objects stay. ERR_LVGL_NOT_INSTALLED when empty. */
MUST_USE err_h sys_lvgl_stop(void);

/**
 * @brief ROTATION: waits for the last FLUSH, turns the panel (DISPLAY ORIENTATION), then LVGL (lv_display_set_rotation,
 * which redraws the screen).
 * @return ERR_LVGL_NOT_INSTALLED when empty, ERR_DEV_RANGE past 270 deg, or the display's error
 */
MUST_USE err_h sys_lvgl_set_rotation(uint8_t rotation);
MUST_USE err_h sys_lvgl_get_rotation(uint8_t* out);

MUST_USE err_h sys_lvgl_get_status(sys_lvgl_status_t* out);

/**
 * @brief Before anything that makes LVGL allocate (a widget, a longer text): whether `need` bytes fit in LVGL's pool
 * and leave CONFIG_SYS_LVGL_POOL_RESERVE free, for what LVGL allocates by itself while it draws. LVGL does not survive
 * a full pool (SYS_LVGL.MD "The pool"): the layer that makes widgets calls this first and refuses with its error.
 * @return ERR_LVGL_NOT_INSTALLED when empty, ERR_LVGL_POOL_FULL{free, need, reserve}
 */
MUST_USE err_h sys_lvgl_pool_check(uint32_t need);

/** @brief Called by RESET under the LVGL lock before the display is deleted: the upper layer drops its widgets. */
typedef void (*sys_lvgl_reset_f)(void);

/** @brief Sets the reset hook (sys_screen); boot only, one. */
void sys_lvgl_set_reset_hook(sys_lvgl_reset_f fn);

/**
 * @brief A device's state changed (the registry's state listener, runit_error_policy.c): for the installed display
 * (or SYS_DEVICE_ID_NONE, every device) it sets a bit of the LVGL task: SUSPENDED pauses drawing, RESUMED and RESET set
 * the rotation again and redraw. Never waits; takes no lock.
 */
void sys_lvgl_device_state(uint16_t id, sys_device_state_e change);

#ifdef __cplusplus
}
#endif
