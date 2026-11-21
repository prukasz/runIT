#include "board_timer.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "utils.h"

/* The running timers, a list in no order (there are few). Under s_lock, which guards short walks and copies only and
 * is never held while a callback runs. */
static board_timer_t* s_running;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

U_TASK_DEFINE(board_timer_task_h, CONFIG_BOARD_TIMER_TASK_STACK);
static bool s_task_claimed;  // under s_lock: the first start claims the task's start, so it starts once

// `at` has come (right across the tick count's wrap).
static bool due(TickType_t now, uint32_t at) {
  return (int32_t)(now - at) >= 0;
}

// Under s_lock.
static void list_remove(board_timer_t* t) {
  for (board_timer_t** p = &s_running; *p != NULL; p = &(*p)->next) {
    if (*p == t) {
      *p = t->next;
      break;
    }
  }
  t->next = NULL;
  t->running = false;
}

// Under s_lock. The due timer that has waited longest (NULL when none is due); else `*wait` is the time to the nearest.
static board_timer_t* longest_due(TickType_t now, TickType_t* wait) {
  board_timer_t* pick = NULL;
  uint32_t pick_late = 0;
  *wait = portMAX_DELAY;
  for (board_timer_t* t = s_running; t != NULL; t = t->next) {
    if (due(now, t->due)) {
      const uint32_t late = now - t->due;
      if (pick == NULL || late > pick_late) {
        pick = t;
        pick_late = late;
      }
    } else {
      const TickType_t left = t->due - now;
      *wait = left < *wait ? left : *wait;
    }
  }
  return pick;
}

/* One due timer at a time, the one that has waited longest: a callback slower than its own period is due again when it
 * ends, and would otherwise run again before the others. It is moved on (its next time, or out of the list for a once)
 * under the lock, its callback copied out and called without it, then the list is looked at again (a callback may have
 * stopped or started timers). With none due the task sleeps until the nearest, or until a start wakes it. */
static void timer_task(void* arg) {
  (void)arg;
  while (1) {
    board_timer_f fn = NULL;
    void* ctx = NULL;
    TickType_t wait = portMAX_DELAY;
    const TickType_t now = xTaskGetTickCount();
    taskENTER_CRITICAL(&s_lock);
    board_timer_t* t = longest_due(now, &wait);
    if (t != NULL) {
      fn = t->fn;
      ctx = t->ctx;
      if (t->period == 0) {
        list_remove(t);
      } else {
        t->due += t->period;
        if (due(now, t->due)) {
          t->due = now + t->period;  // late: the missed periods are skipped, not run in a burst
        }
      }
    }
    taskEXIT_CRITICAL(&s_lock);
    if (fn != NULL) {
      fn(ctx);
      continue;
    }
    (void)ulTaskNotifyTake(pdTRUE, wait);
  }
}

// Puts `t` in the list due `after` ticks from now and wakes the task (it may sleep past `t`'s time).
static void arm(board_timer_t* t, uint32_t period, uint32_t after) {
  const TickType_t now = xTaskGetTickCount();
  bool start_task = false;
  taskENTER_CRITICAL(&s_lock);
  if (!t->running) {
    t->next = s_running;
    s_running = t;
    t->running = true;
  }
  t->period = period;
  t->due = now + after;
  if (!s_task_claimed) {
    s_task_claimed = true;
    start_task = true;
  }
  taskEXIT_CRITICAL(&s_lock);
  if (start_task) {
    U_TASK_START(board_timer_task_h, timer_task, NULL, CONFIG_BOARD_TIMER_TASK_PRIO);
  }
  if (board_timer_task_h != NULL) {
    xTaskNotifyGive(board_timer_task_h);
  }
}

void board_timer_init(board_timer_t* t, board_timer_f fn, void* ctx) {
  *t = (board_timer_t){.fn = fn, .ctx = ctx};
}

void board_timer_start(board_timer_t* t, uint32_t period_ms) {
  TickType_t period = MSEC(period_ms);
  period = period > 0 ? period : 1;
  taskENTER_CRITICAL(&s_lock);
  const bool same = t->running && t->period == period;
  taskEXIT_CRITICAL(&s_lock);
  if (!same) {
    arm(t, period, period);
  }
}

void board_timer_once(board_timer_t* t, uint32_t after_ms) {
  arm(t, 0, MSEC(after_ms));
}

void board_timer_stop(board_timer_t* t) {
  taskENTER_CRITICAL(&s_lock);
  if (t->running) {
    list_remove(t);
  }
  taskEXIT_CRITICAL(&s_lock);
}

bool board_timer_running(const board_timer_t* t) {
  taskENTER_CRITICAL(&s_lock);
  const bool running = t->running;
  taskEXIT_CRITICAL(&s_lock);
  return running;
}
