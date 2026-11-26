#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos_shim.h"

/* ---- time ----------------------------------------------------------------------------------------------------- */

/* Waits on `c` (with `m` held) until `ticks` (ms) pass; portMAX_DELAY waits for the signal. Returns false on timeout. */
static bool wait_cond(pthread_cond_t* c, pthread_mutex_t* m, TickType_t ticks) {
  if (ticks == portMAX_DELAY) {
    pthread_cond_wait(c, m);
    return true;
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += ticks / 1000;
  ts.tv_nsec += (long)(ticks % 1000) * 1000000L;
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000L;
  }
  return pthread_cond_timedwait(c, m, &ts) != ETIMEDOUT;
}

/* ---- the scheduler -------------------------------------------------------------------------------------------- */

/* As on the board: every constructor has run before a task does. A task a constructor makes waits here, so it never
 * sees a static object (utils.h, its component's own) that a later constructor makes. */
static pthread_mutex_t s_sched_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_sched_c = PTHREAD_COND_INITIALIZER;
static bool s_sched_started;

static void scheduler_wait(void) {
  pthread_mutex_lock(&s_sched_m);
  while (!s_sched_started) {
    pthread_cond_wait(&s_sched_c, &s_sched_m);
  }
  pthread_mutex_unlock(&s_sched_m);
}

static void scheduler_start(void) {
  pthread_mutex_lock(&s_sched_m);
  s_sched_started = true;
  pthread_cond_broadcast(&s_sched_c);
  pthread_mutex_unlock(&s_sched_m);
}

/* The executable's main (a test's own is built as host_test_main, run.py): the constructors are done by now. */
#undef main
int host_test_main(void);

int main(void) {
  scheduler_start();
  return host_test_main();
}

/* ---- tasks: threads ------------------------------------------------------------------------------------------- */

typedef struct {
  pthread_mutex_t m;
  pthread_cond_t c;
  uint32_t notified;
  TaskFunction_t fn;
  void* arg;
} task_t;

static __thread task_t* s_self;
static __thread int s_in_isr;

static task_t* task_new(TaskFunction_t fn, void* arg) {
  task_t* t = calloc(1, sizeof(*t));
  pthread_mutex_init(&t->m, NULL);
  pthread_cond_init(&t->c, NULL);
  t->fn = fn;
  t->arg = arg;
  return t;
}

BaseType_t xPortInIsrContext(void) {
  return s_in_isr ? pdTRUE : pdFALSE;
}

void host_isr_set(int in_isr) {
  s_in_isr = in_isr;
}

static void* task_main(void* p) {
  task_t* t = p;
  s_self = t;
  scheduler_wait();
  t->fn(t->arg);
  return NULL;
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t fn, const char* name, uint32_t stack_depth, void* arg, UBaseType_t prio,
                               StackType_t* stack, StaticTask_t* tcb) {
  (void)name, (void)stack_depth, (void)prio, (void)stack, (void)tcb;
  task_t* t = task_new(fn, arg);
  pthread_t th;
  pthread_create(&th, NULL, task_main, t);
  pthread_detach(th);
  return t;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) {
  if (s_self == NULL) {
    s_self = task_new(NULL, NULL);  /* a thread not made here (the test's own) is a task too, never NULL */
  }
  return s_self;
}

void host_task_notify_give(TaskHandle_t task) {
  task_t* t = task;
  pthread_mutex_lock(&t->m);
  t->notified++;
  pthread_cond_signal(&t->c);
  pthread_mutex_unlock(&t->m);
}

uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks) {
  task_t* t = xTaskGetCurrentTaskHandle();
  pthread_mutex_lock(&t->m);
  while (t->notified == 0) {
    if (!wait_cond(&t->c, &t->m, ticks)) {
      break;
    }
  }
  uint32_t value = t->notified;
  if (value > 0) {
    t->notified = clear_on_exit ? 0 : value - 1;
  }
  pthread_mutex_unlock(&t->m);
  return value;
}

/* ---- ring buffer ---------------------------------------------------------------------------------------------- */

typedef struct rb_item {
  struct rb_item* next;
  size_t size;
  size_t cost;
  bool complete;
  uint8_t data[] __attribute__((aligned(8)));
} rb_item_t;

typedef struct {
  pthread_mutex_t m;
  pthread_cond_t c;
  size_t cap;
  size_t used;         /* bytes of items acquired and not yet returned */
  UBaseType_t items;   /* items currently in ring (acquired + queued) */
  rb_item_t* head;     /* queued, oldest first (acquired or complete, not yet received) */
  rb_item_t* tail;
} rb_t;

RingbufHandle_t xRingbufferCreate(size_t size, RingbufferType_t type) {
  return xRingbufferCreateStatic(size, type, NULL, NULL);
}

RingbufHandle_t xRingbufferCreateStatic(size_t size, RingbufferType_t type, uint8_t* storage, StaticRingbuffer_t* st) {
  (void)type, (void)storage, (void)st;
  rb_t* rb = calloc(1, sizeof(*rb));
  pthread_mutex_init(&rb->m, NULL);
  pthread_cond_init(&rb->c, NULL);
  rb->cap = size;
  return rb;
}

void vRingbufferDelete(RingbufHandle_t h) {
  rb_t* rb = h;
  if (rb == NULL) {
    return;
  }
  rb_item_t* it = rb->head;
  while (it != NULL) {
    rb_item_t* next = it->next;
    free(it);
    it = next;
  }
  pthread_mutex_destroy(&rb->m);
  pthread_cond_destroy(&rb->c);
  free(rb);
}

void vRingbufferGetInfo(RingbufHandle_t h, UBaseType_t* free_pos, UBaseType_t* read_pos, UBaseType_t* write_pos,
                        UBaseType_t* acq_pos, UBaseType_t* items_waiting) {
  rb_t* rb = h;
  pthread_mutex_lock(&rb->m);
  if (free_pos != NULL) *free_pos = 0;
  if (read_pos != NULL) *read_pos = 0;
  if (write_pos != NULL) *write_pos = (UBaseType_t)(rb->used % rb->cap);
  if (acq_pos != NULL) *acq_pos = (UBaseType_t)(rb->used % rb->cap);
  if (items_waiting != NULL) *items_waiting = rb->items;
  pthread_mutex_unlock(&rb->m);
}

BaseType_t xRingbufferSendAcquire(RingbufHandle_t h, void** item, size_t size, TickType_t ticks) {
  (void)ticks;  /* never waits for room */
  rb_t* rb = h;
  const size_t cost = 8 + ((size + 3) & ~(size_t)3);
  pthread_mutex_lock(&rb->m);
  if (rb->used + cost > rb->cap) {
    pthread_mutex_unlock(&rb->m);
    return pdFALSE;
  }
  rb_item_t* it = calloc(1, sizeof(*it) + size);
  it->size = size;
  it->cost = cost;
  rb->used += cost;
  rb->items++;
  if (rb->tail != NULL) {
    rb->tail->next = it;
  } else {
    rb->head = it;
  }
  rb->tail = it;
  pthread_mutex_unlock(&rb->m);
  *item = it->data;
  return pdTRUE;
}

BaseType_t xRingbufferSendComplete(RingbufHandle_t h, void* data) {
  rb_t* rb = h;
  rb_item_t* it = (rb_item_t*)((uint8_t*)data - offsetof(rb_item_t, data));
  pthread_mutex_lock(&rb->m);
  it->complete = true;
  pthread_cond_broadcast(&rb->c);
  pthread_mutex_unlock(&rb->m);
  return pdTRUE;
}

BaseType_t xRingbufferSend(RingbufHandle_t h, const void* data, size_t size, TickType_t ticks) {
  void* item = NULL;
  if (xRingbufferSendAcquire(h, &item, size, ticks) != pdTRUE) {
    return pdFALSE;
  }
  memcpy(item, data, size);
  return xRingbufferSendComplete(h, item);
}

BaseType_t xRingbufferSendFromISR(RingbufHandle_t h, const void* data, size_t size, BaseType_t* woken) {
  *woken = pdFALSE;
  return xRingbufferSend(h, data, size, 0);
}

void* xRingbufferReceive(RingbufHandle_t h, size_t* size, TickType_t ticks) {
  rb_t* rb = h;
  pthread_mutex_lock(&rb->m);
  while (rb->head == NULL || !rb->head->complete) {
    if (ticks == 0 || !wait_cond(&rb->c, &rb->m, ticks)) {
      pthread_mutex_unlock(&rb->m);
      return NULL;
    }
  }
  rb_item_t* it = rb->head;
  rb->head = it->next;
  if (rb->head == NULL) {
    rb->tail = NULL;
  }
  pthread_mutex_unlock(&rb->m);
  *size = it->size;
  return it->data;
}

void vRingbufferReturnItem(RingbufHandle_t h, void* data) {
  rb_t* rb = h;
  rb_item_t* it = (rb_item_t*)((uint8_t*)data - offsetof(rb_item_t, data));
  pthread_mutex_lock(&rb->m);
  rb->used -= it->cost;
  if (rb->items > 0) rb->items--;
  pthread_mutex_unlock(&rb->m);
  free(it);
}

void vTaskDelay(TickType_t ticks) {
  struct timespec ts = {.tv_sec = ticks / 1000, .tv_nsec = (long)(ticks % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack_depth, void* arg, UBaseType_t prio,
                       TaskHandle_t* out_handle) {
  TaskHandle_t h = xTaskCreateStatic(fn, name, stack_depth, arg, prio, NULL, NULL);
  if (out_handle != NULL) {
    *out_handle = h;
  }
  return h != NULL ? pdPASS : pdFALSE;
}

void vTaskDelete(TaskHandle_t task) {
  if (task == NULL) {
    pthread_exit(NULL);
  }
}

TickType_t xTaskGetTickCount(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (TickType_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

BaseType_t xTaskGetSchedulerState(void) {
  return taskSCHEDULER_RUNNING;
}

UBaseType_t uxTaskGetSystemState(TaskStatus_t* arr, UBaseType_t arr_size, uint32_t* total_run_time) {
  if (total_run_time != NULL) *total_run_time = 0;
  if (arr_size < 1) return 0;
  arr[0] = (TaskStatus_t){.pcTaskName = "host_main", .uxCurrentPriority = 5, .usStackHighWaterMark = 1024};
  return 1;
}

UBaseType_t uxTaskGetNumberOfTasks(void) {
  return 1;
}

/* ---- semaphores ----------------------------------------------------------------------------------------------- */

typedef struct {
  pthread_mutex_t m;
  pthread_cond_t c;
  bool recursive;
  bool mutex;          /* a mutex: given at the start, `holder` is the task that took it */
  uint32_t count;      /* binary / mutex: 0 or 1 given; recursive: how deep the owner holds it */
  pthread_t owner;
  TaskHandle_t holder;
} sem_t_;

static sem_t_* sem_new(bool recursive) {
  sem_t_* s = calloc(1, sizeof(*s));
  pthread_mutex_init(&s->m, NULL);
  pthread_cond_init(&s->c, NULL);
  s->recursive = recursive;
  return s;
}

SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* buffer) {
  (void)buffer;
  return sem_new(false);
}

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* buffer) {
  (void)buffer;
  sem_t_* s = sem_new(false);
  s->mutex = true;
  s->count = 1;
  return s;
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t* buffer) {
  (void)buffer;
  return sem_new(true);
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t ticks) {
  sem_t_* s = h;
  TaskHandle_t self = xTaskGetCurrentTaskHandle();
  pthread_mutex_lock(&s->m);
  while (s->count == 0) {
    if (ticks == 0 || !wait_cond(&s->c, &s->m, ticks)) {
      break;
    }
  }
  const bool taken = s->count > 0;
  if (taken) {
    s->count = 0;
    s->holder = s->mutex ? self : NULL;
  }
  pthread_mutex_unlock(&s->m);
  return taken ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {
  sem_t_* s = h;
  pthread_mutex_lock(&s->m);
  const bool given = s->count == 0;
  s->count = 1;
  s->holder = NULL;
  pthread_cond_signal(&s->c);
  pthread_mutex_unlock(&s->m);
  return given ? pdTRUE : pdFALSE;
}

TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t h) {
  sem_t_* s = h;
  pthread_mutex_lock(&s->m);
  TaskHandle_t holder = (s->recursive ? s->count > 0 : s->count == 0) ? s->holder : NULL;
  pthread_mutex_unlock(&s->m);
  return holder;
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t h, TickType_t ticks) {
  sem_t_* s = h;
  const pthread_t self = pthread_self();
  TaskHandle_t self_task = xTaskGetCurrentTaskHandle();
  pthread_mutex_lock(&s->m);
  while (s->count > 0 && !pthread_equal(s->owner, self)) {
    if (ticks == 0 || !wait_cond(&s->c, &s->m, ticks)) {
      break;
    }
  }
  const bool taken = s->count == 0 || pthread_equal(s->owner, self);
  if (taken) {
    s->owner = self;
    s->holder = self_task;
    s->count++;
  }
  pthread_mutex_unlock(&s->m);
  return taken ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t h) {
  sem_t_* s = h;
  pthread_mutex_lock(&s->m);
  const bool held = s->count > 0 && pthread_equal(s->owner, pthread_self());
  if (held && --s->count == 0) {
    pthread_cond_signal(&s->c);
  }
  pthread_mutex_unlock(&s->m);
  return held ? pdTRUE : pdFALSE;
}
