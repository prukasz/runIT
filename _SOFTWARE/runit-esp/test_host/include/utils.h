#pragma once
/* Host stand-in for core/utils: only what errors uses (the real one pulls in all of FreeRTOS). */
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MUST_USE     __attribute__((warn_unused_result))

#include "freertos_shim.h"

#define NO_WAIT  0  // a timeout of 0 ticks; forever is portMAX_DELAY
#define MSEC(ms) ((TickType_t)(ms))  // a tick is a millisecond here

static inline void u_ringbuf_clear(RingbufHandle_t rb) {
  size_t size = 0;
  void* item = NULL;
  while (rb != NULL && (item = xRingbufferReceive(rb, &size, 0)) != NULL) {
    vRingbufferReturnItem(rb, item);
  }
}

/* The critical section (portMUX_TYPE, portENTER_CRITICAL...) is freertos_shim.h's, as on the board it is FreeRTOS's. */

static inline SemaphoreHandle_t u__mutex_take(SemaphoreHandle_t mtx) {
  (void)xSemaphoreTake(mtx, portMAX_DELAY);
  return mtx;
}

static inline void u__mutex_release(SemaphoreHandle_t* mtx) {
  if (*mtx != NULL) {
    (void)xSemaphoreGive(*mtx);
  }
}

static inline SemaphoreHandle_t u__rmutex_take(SemaphoreHandle_t mtx) {
  (void)xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
  return mtx;
}

static inline void u__rmutex_release(SemaphoreHandle_t* mtx) {
  if (*mtx != NULL) {
    (void)xSemaphoreGiveRecursive(*mtx);
  }
}

#define U__CONCAT2(a, b) a##b
#define U__CONCAT(a, b)  U__CONCAT2(a, b)

#define U_MUTEX_GUARD(mtx)                                                                                         \
  SemaphoreHandle_t U__CONCAT(_u_mtx_guard_, __COUNTER__) __attribute__((cleanup(u__mutex_release), unused)) = \
      u__mutex_take(mtx)

#define U_RMUTEX_GUARD(mtx)                                                                                         \
  SemaphoreHandle_t U__CONCAT(_u_rmtx_guard_, __COUNTER__) __attribute__((cleanup(u__rmutex_release), unused)) = \
      u__rmutex_take(mtx)
