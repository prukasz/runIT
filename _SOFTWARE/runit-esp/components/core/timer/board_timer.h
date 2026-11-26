#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The board's timers: one place that runs code later or every so often (TIMER.MD). Everything that needs a
 * timer (a driver's ramp or poll, a module's timeout) takes one here, never an esp_timer or a FreeRTOS timer of its
 * own, so the platform under it is swapped in one place (the host tests run it on their FreeRTOS stand-ins).
 *
 * The callbacks run in the timers' task (CONFIG_BOARD_TIMER_TASK_PRIO / _STACK), one after another: never in an ISR
 * nor on the esp_timer task, so a slow callback (I2C) holds up only the other board timers, and it may block on I/O,
 * never sleep or wait long for a lock. Of the timers due, the one that has waited longest runs first: a callback slower
 * than its period does not starve the others. The time is the RTOS tick (1 ms).
 *
 * A timer is a board_timer_t in its owner's struct: no table to fill, no error. After board_timer_stop() no new call
 * of its callback starts, but one already started runs to its end: a callback whose owner can go away (a device) takes
 * an ID as its ctx, not a pointer, and the owner stops the timer before it frees the struct. A callback that runs late
 * skips the periods it missed; they are not run in a burst.
 *
 * Any task, not an ISR.
 */

/** @brief The step of the timers that count time in steps (a call's timeout, a session's ping, a ramp), in ms. */
#define BOARD_TIMER_STEP_MS 10u

typedef void (*board_timer_f)(void* ctx);

/** @brief A timer: in its owner's struct, set up by board_timer_init(); its fields are the module's. */
typedef struct board_timer_s {
  struct board_timer_s* next;  // in the running list
  board_timer_f fn;
  void* ctx;
  uint32_t period;             // ticks; 0: once
  uint32_t due;                // tick when it runs next
  bool running;
} board_timer_t;

/**
 * @brief Sets up a stopped timer. Not on a running one.
 * @param[out] t   The timer
 * @param[in]  fn  Its callback, in the timers' task
 * @param[in]  ctx Passed to fn; an ID when what it names can go away
 */
void board_timer_init(board_timer_t* t, board_timer_f fn, void* ctx);

/**
 * @brief Runs the callback every `period_ms`, the first time `period_ms` from now. A running timer with the same
 * period goes on as it was (its phase stays); with another period, or a once, it starts again from now.
 * @param[in] t         The timer
 * @param[in] period_ms The period, 1 ms at least (0 counts as 1)
 */
void board_timer_start(board_timer_t* t, uint32_t period_ms);

/**
 * @brief Runs the callback once, `after_ms` from now (0: as soon as the task gets to it). A running timer starts again.
 * @param[in] t        The timer
 * @param[in] after_ms When
 */
void board_timer_once(board_timer_t* t, uint32_t after_ms);

/**
 * @brief Stops the timer (a stopped one: nothing). No new call starts; one already started runs to its end. Also
 * before the owner frees the struct the timer is in.
 * @param[in] t The timer
 */
void board_timer_stop(board_timer_t* t);

#ifdef __cplusplus
}
#endif
