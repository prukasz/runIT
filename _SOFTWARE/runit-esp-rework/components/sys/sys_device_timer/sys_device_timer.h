#pragma once

#include <stdint.h>

#include "board_timer.h"
#include "sys_device_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A device's timer: a board timer (board_timer.h) whose expiry is a step of its owner (sys_device_step), and a
 * resource of it. The timers' task writes the op (no args) to the owner by its ID and generation, under the device's
 * lock, only when the device takes it now: a step is skipped, with no error, when the device is busy (another task
 * holds its lock), suspended, refused by the safe-state sweep, or gone (a device created later under its ID included).
 * So a busy device never holds up the other timers, and the step's handler runs only on a working device: it needs no
 * flag of its own for the safe state. The registry stops the timer when the owner is removed or its create() fails,
 * before it frees the struct the timer is in: a driver never does. The op is the driver's own and SYS_DEVICE_OP_LOCAL
 * (not a command). A step that fails is reported each time it runs, so its handler stops the timer on a failure.
 */

/** @brief A device's timer, in the driver's struct (zeroed: not set up). */
typedef struct {
  sys_device_res_t res;  /**< The timer as a resource of its owner: the registry's */
  board_timer_t timer;
} sys_device_timer_t;

/**
 * @brief Sets up a stopped timer of device `owner` whose expiry is the step `op` of it. Called from the owner's
 * create(), which already has the device's generation (sys_device_gen).
 * @param[in]  owner The device being created
 * @param[out] t     The timer, not set up yet
 * @param[in]  op    The owner's own op (SYS_DEVICE_OWN_OP) the timers' task writes, without args
 * @return ERR_DEV_BAD_ARG for an op not the device's own, else the error of sys_device_res_add
 * @usage ERR_TRY(sys_device_timer_init(id, &c->timer, OWN_DC_MOTOR_OP_TICK));
 */
MUST_USE err_h sys_device_timer_init(uint8_t owner, sys_device_timer_t* t, uint16_t op);

/** @brief board_timer_start: the op every `period_ms`; a running timer with the same period keeps its phase. */
static inline void sys_device_timer_start(sys_device_timer_t* t, uint32_t period_ms) {
  board_timer_start(&t->timer, period_ms);
}

/** @brief board_timer_once: the op once, `after_ms` from now. */
static inline void sys_device_timer_once(sys_device_timer_t* t, uint32_t after_ms) {
  board_timer_once(&t->timer, after_ms);
}

/** @brief board_timer_stop: no new step starts. One already on its way may still run once after the caller's op (it
 * is skipped while that op holds the lock): its handler finds the state the op left. */
static inline void sys_device_timer_stop(sys_device_timer_t* t) {
  board_timer_stop(&t->timer);
}

#ifdef __cplusplus
}
#endif
