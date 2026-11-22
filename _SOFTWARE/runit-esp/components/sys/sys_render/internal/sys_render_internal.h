/**
 * @file
 * @brief Internal functions of sys_render, not for applications.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One turn of the render task, under the render lock: handles the display's events the
 * listener flagged (SUSPENDED, RESUMED, RESET, REMOVED) and, while the adapter is RUNNING, calls
 * the engine's timer_handler. The task loop waits for a notification or the engine's next timer
 * and calls this; tests call it directly. Does nothing when the adapter is empty. A failed read of
 * the display's INFO is reported (err_report) and the state stays as it was.
 * @return Milliseconds until the next turn is due (at most SYS_RENDER_TASK_SLEEP_MAX_MS), or
 * SYS_RENDER_TASK_SLEEP_FOREVER when there is nothing to draw until the task is woken
 */
uint32_t sys_render_task_step(void);

#ifdef __cplusplus
}
#endif
