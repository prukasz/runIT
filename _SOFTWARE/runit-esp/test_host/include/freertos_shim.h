#pragma once
/* Host stand-in for the part of FreeRTOS / esp_ringbuf the components use: tasks are threads, a ring buffer is a
 * locked list with the byte accounting of a no-split ring (header 8 B + the size rounded up to 4 per item). Enough to
 * run a component's tasks for real on the PC (src/host_freertos.c); not a model of FreeRTOS scheduling or priorities. */
#include <stddef.h>
#include <stdint.h>

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef uint32_t UBaseType_t;
typedef void* TaskHandle_t;
typedef uint8_t StackType_t;
typedef struct { int unused; } StaticTask_t;
typedef void (*TaskFunction_t)(void*);
typedef void* RingbufHandle_t;
typedef struct { int unused; } StaticRingbuffer_t;
typedef enum { RINGBUF_TYPE_NOSPLIT, RINGBUF_TYPE_ALLOWSPLIT, RINGBUF_TYPE_BYTEBUF } RingbufferType_t;

#define pdTRUE        1
#define pdFALSE       0
#define pdPASS        1
#define portMAX_DELAY 0xFFFFFFFFu

/* A tick is a millisecond. */
#define configTICK_RATE_HZ    1000u
#define pdMS_TO_TICKS(ms)     ((TickType_t)(ms))
#define portYIELD_FROM_ISR(x) ((void)(x))  // an ISR is a thread here
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);  // the milliseconds of a monotonic clock

/* Semaphores: a binary semaphore and a recursive mutex (src/host_freertos.c); FromISR is the same call. */
typedef void* SemaphoreHandle_t;
typedef struct { int unused; } StaticSemaphore_t;
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* buffer);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t* buffer);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t mutex, TickType_t ticks);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t mutex);
#define xSemaphoreGiveFromISR(sem, woken) (*(woken) = pdFALSE, xSemaphoreGive(sem))

/* The critical section of a task (taskENTER_CRITICAL) is the process-wide one of utils.h. */
#define taskENTER_CRITICAL(m) portENTER_CRITICAL_SAFE(m)
#define taskEXIT_CRITICAL(m)  portEXIT_CRITICAL_SAFE(m)

TaskHandle_t xTaskCreateStatic(TaskFunction_t fn, const char* name, uint32_t stack_depth, void* arg, UBaseType_t prio,
                               StackType_t* stack, StaticTask_t* tcb);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void host_task_notify_give(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks);
#define xTaskNotifyGive(task) host_task_notify_give(task)

RingbufHandle_t xRingbufferCreateStatic(size_t size, RingbufferType_t type, uint8_t* storage, StaticRingbuffer_t* st);
BaseType_t xRingbufferSendAcquire(RingbufHandle_t rb, void** item, size_t size, TickType_t ticks);
BaseType_t xRingbufferSendComplete(RingbufHandle_t rb, void* item);
void* xRingbufferReceive(RingbufHandle_t rb, size_t* size, TickType_t ticks);
void vRingbufferReturnItem(RingbufHandle_t rb, void* item);

/* The static-object macros of core/utils (utils.h), with the same names and meaning. */
#define U_RINGBUFFER_DEFINE(name, buffer_size, type)                                             \
  static uint8_t _##name##_storage[(buffer_size)] __attribute__((aligned(4)));                   \
  static StaticRingbuffer_t _##name##_struct;                                                    \
  RingbufHandle_t name = NULL;                                                                   \
  __attribute__((constructor)) static void _init_##name(void) {                                  \
    name = xRingbufferCreateStatic((buffer_size), (type), _##name##_storage, &_##name##_struct); \
  }

#define U_BINARY_SEM_DEFINE(name)                               \
  static StaticSemaphore_t _##name##_buffer;                    \
  SemaphoreHandle_t name = NULL;                                \
  __attribute__((constructor)) static void _init_##name(void) { \
    name = xSemaphoreCreateBinaryStatic(&_##name##_buffer);     \
  }

#define U_RECURSIVE_MUTEX_DEFINE(name)                              \
  static StaticSemaphore_t _##name##_buffer;                        \
  SemaphoreHandle_t name = NULL;                                    \
  __attribute__((constructor)) static void _init_##name(void) {     \
    name = xSemaphoreCreateRecursiveMutexStatic(&_##name##_buffer); \
  }

#define U_TASK_DEFINE(name, stack_words)             \
  static StackType_t _##name##_stack[(stack_words)]; \
  static StaticTask_t _##name##_tcb;                 \
  TaskHandle_t name = NULL

#define U_TASK_START(name, task_func, param, priority)                                                       \
  do {                                                                                                       \
    name = xTaskCreateStatic((task_func), #name, (sizeof(_##name##_stack) / sizeof(StackType_t)), (param),   \
                             (priority), _##name##_stack, &_##name##_tcb);                                   \
  } while (0)
