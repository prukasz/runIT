#pragma once

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/message_buffer.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * General
 * ------------------------------------------------------------------------- */

/**
 * @brief Element count of a real array (not a pointer).
 */
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/**
 * @brief Compiler warns if the caller ignores the return value.
 * @usage MUST_USE err_h foo_init(void);
 */
#define MUST_USE __attribute__((warn_unused_result))

/* ----------------------------------------------------------------------------
 * Time
 * ------------------------------------------------------------------------- */

/**
 * @brief Milliseconds to ticks, rounded UP - any non-zero time waits at least
 * one tick (pdMS_TO_TICKS rounds down, so short timeouts could become 0).
 */
#define MSEC(ms) ((TickType_t)(((uint64_t)(ms) * configTICK_RATE_HZ + 999u) / 1000u))
#define NO_WAIT  0  // a timeout of 0 ticks; forever is portMAX_DELAY

/* ----------------------------------------------------------------------------
 * Static object definitions
 *
 * Each U_*_DEFINE(name, ...) allocates the object's storage statically and
 * creates it in a constructor (before app_main), so the handle `name` is
 * valid from the first line of app_main. Use at file scope only.
 * ------------------------------------------------------------------------- */

/**
 * @brief Static ESP-IDF ring buffer. `type` is RINGBUF_TYPE_*.
 */
#define U_RINGBUFFER_DEFINE(name, buffer_size, type)                                             \
  /* xRingbufferCreateStatic asserts this at boot; checked at build time here */                 \
  _Static_assert((type) == RINGBUF_TYPE_BYTEBUF || (buffer_size) % 4 == 0,                       \
                 #name ": a NOSPLIT / ALLOWSPLIT size is a multiple of 4");                      \
  static uint8_t _##name##_storage[(buffer_size)] __attribute__((aligned(4)));                   \
  static StaticRingbuffer_t _##name##_struct;                                                    \
  RingbufHandle_t name = NULL;                                                                   \
  __attribute__((constructor)) static void _init_##name(void) {                                  \
    name = xRingbufferCreateStatic((buffer_size), (type), _##name##_storage, &_##name##_struct); \
  }

/**
 * @brief Static event group.
 */
#define U_EVENT_GROUP_DEFINE(name)                              \
  static StaticEventGroup_t _##name##_buffer;                   \
  EventGroupHandle_t name = NULL;                               \
  __attribute__((constructor)) static void _init_##name(void) { \
    name = xEventGroupCreateStatic(&_##name##_buffer);          \
  }

/**
 * @brief Static mutex.
 */
#define U_MUTEX_DEFINE(name)                                    \
  static StaticSemaphore_t _##name##_buffer;                    \
  SemaphoreHandle_t name = NULL;                                \
  __attribute__((constructor)) static void _init_##name(void) { \
    name = xSemaphoreCreateMutexStatic(&_##name##_buffer);      \
  }

/**
 * @brief Static recursive mutex.
 */
#define U_RECURSIVE_MUTEX_DEFINE(name)                              \
  static StaticSemaphore_t _##name##_buffer;                        \
  SemaphoreHandle_t name = NULL;                                    \
  __attribute__((constructor)) static void _init_##name(void) {     \
    name = xSemaphoreCreateRecursiveMutexStatic(&_##name##_buffer); \
  }

/**
 * @brief Static binary semaphore (created empty).
 */
#define U_BINARY_SEM_DEFINE(name)                               \
  static StaticSemaphore_t _##name##_buffer;                    \
  SemaphoreHandle_t name = NULL;                                \
  __attribute__((constructor)) static void _init_##name(void) { \
    name = xSemaphoreCreateBinaryStatic(&_##name##_buffer);     \
  }

/**
 * @brief Static counting semaphore.
 */
#define U_COUNTING_SEM_DEFINE(name, max_count, initial_count)                               \
  static StaticSemaphore_t _##name##_buffer;                                                \
  SemaphoreHandle_t name = NULL;                                                            \
  __attribute__((constructor)) static void _init_##name(void) {                             \
    name = xSemaphoreCreateCountingStatic((max_count), (initial_count), &_##name##_buffer); \
  }

/**
 * @brief Static queue of `length` items, `item_size` bytes each.
 */
#define U_QUEUE_DEFINE(name, length, item_size)                                             \
  static uint8_t _##name##_storage[(length) * (item_size)] __attribute__((aligned(4)));     \
  static StaticQueue_t _##name##_buffer;                                                    \
  QueueHandle_t name = NULL;                                                                \
  __attribute__((constructor)) static void _init_##name(void) {                             \
    name = xQueueCreateStatic((length), (item_size), _##name##_storage, &_##name##_buffer); \
  }

/**
 * @brief Static stream buffer.
 */
#define U_STREAM_BUFFER_DEFINE(name, buffer_size, trigger_level)                                            \
  static uint8_t _##name##_storage[(buffer_size)] __attribute__((aligned(4)));                              \
  static StaticStreamBuffer_t _##name##_buffer;                                                             \
  StreamBufferHandle_t name = NULL;                                                                         \
  __attribute__((constructor)) static void _init_##name(void) {                                             \
    name = xStreamBufferCreateStatic((buffer_size), (trigger_level), _##name##_storage, &_##name##_buffer); \
  }

/**
 * @brief Static message buffer.
 */
#define U_MESSAGE_BUFFER_DEFINE(name, buffer_size)                                          \
  static uint8_t _##name##_storage[(buffer_size)] __attribute__((aligned(4)));              \
  static StaticMessageBuffer_t _##name##_buffer;                                            \
  MessageBufferHandle_t name = NULL;                                                        \
  __attribute__((constructor)) static void _init_##name(void) {                             \
    name = xMessageBufferCreateStatic((buffer_size), _##name##_storage, &_##name##_buffer); \
  }

/**
 * @brief Static software timer. `callback_func` is a TimerCallbackFunction_t.
 */
#define U_TIMER_DEFINE(name, period_ticks, auto_reload, callback_func)                                                            \
  static StaticTimer_t _##name##_buffer;                                                                                          \
  TimerHandle_t name = NULL;                                                                                                      \
  __attribute__((constructor)) static void _init_##name(void) {                                                                   \
    name = xTimerCreateStatic(#name, (period_ticks), (auto_reload) ? pdTRUE : pdFALSE, NULL, (callback_func), &_##name##_buffer); \
  }

/**
 * @brief Static task storage. The task is not started - use U_TASK_START.
 */
#define U_TASK_DEFINE(name, stack_words)             \
  static StackType_t _##name##_stack[(stack_words)]; \
  static StaticTask_t _##name##_tcb;                 \
  TaskHandle_t name = NULL

/**
 * @brief Starts a task defined with U_TASK_DEFINE.
 */
#define U_TASK_START(name, task_func, param, priority)                                                                                                   \
  do {                                                                                                                                                   \
    name = xTaskCreateStatic((task_func), #name, (sizeof(_##name##_stack) / sizeof(StackType_t)), (param), (priority), _##name##_stack, &_##name##_tcb); \
  } while (0)

/**
 * @brief Starts a task defined with U_TASK_DEFINE, pinned to `core_id`.
 */
#define U_TASK_START_ON_CORE(name, task_func, param, priority, core_id)                                                                                                         \
  do {                                                                                                                                                                          \
    name = xTaskCreateStaticPinnedToCore((task_func), #name, (sizeof(_##name##_stack) / sizeof(StackType_t)), (param), (priority), _##name##_stack, &_##name##_tcb, (core_id)); \
  } while (0)

/* ----------------------------------------------------------------------------
 * Singly linked list
 *
 * Works on any struct with a `next` pointer to its own type. `head` is a
 * plain pointer variable (NULL = empty list).
 * ------------------------------------------------------------------------- */

/**
 * @brief Appends `add` at the tail. O(N).
 */
#define LL_APPEND(head, add)          \
  do {                                \
    (add)->next = NULL;               \
    if ((head) == NULL) {             \
      (head) = (add);                 \
    } else {                          \
      __typeof__(head) _tmp = (head); \
      while (_tmp->next != NULL) {    \
        _tmp = _tmp->next;            \
      }                               \
      _tmp->next = (add);             \
    }                                 \
  } while (0)

/**
 * @brief Inserts `add` at the head. O(1).
 */
#define LL_PREPEND(head, add) \
  do {                        \
    (add)->next = (head);     \
    (head) = (add);           \
  } while (0)

/**
 * @brief Unlinks `del` from the list (does not free it). O(N).
 */
#define LL_DELETE(head, del)                        \
  do {                                              \
    if ((head) == (del)) {                          \
      (head) = (head)->next;                        \
    } else {                                        \
      __typeof__(head) _tmp = (head);               \
      while (_tmp != NULL && _tmp->next != (del)) { \
        _tmp = _tmp->next;                          \
      }                                             \
      if (_tmp != NULL) {                           \
        _tmp->next = (del)->next;                   \
      }                                             \
    }                                               \
  } while (0)

/**
 * @brief Iterates the list. Do not LL_DELETE or free `el` inside the loop -
 * use LL_FOREACH_SAFE for that.
 */
#define LL_FOREACH(head, el) for ((el) = (head); (el) != NULL; (el) = (el)->next)

/**
 * @brief Iterates the list, safe against LL_DELETE / free of `el` - the next
 * element is saved in `tmp` before the body runs.
 */
#define LL_FOREACH_SAFE(head, el, tmp) \
  for ((el) = (head), (tmp) = ((head) != NULL ? (head)->next : NULL); (el) != NULL; (el) = (tmp), (tmp) = ((el) != NULL ? (el)->next : NULL))

#ifdef __cplusplus
}
#endif
