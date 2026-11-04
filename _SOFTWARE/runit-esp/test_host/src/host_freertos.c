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

/* ---- tasks: threads ------------------------------------------------------------------------------------------- */

typedef struct {
  pthread_mutex_t m;
  pthread_cond_t c;
  uint32_t notified;
  TaskFunction_t fn;
  void* arg;
} task_t;

static __thread task_t* s_self;
static task_t s_main_task = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, NULL, NULL};

static void* task_main(void* p) {
  task_t* t = p;
  s_self = t;
  t->fn(t->arg);
  return NULL;
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t fn, const char* name, uint32_t stack_depth, void* arg, UBaseType_t prio,
                               StackType_t* stack, StaticTask_t* tcb) {
  (void)name, (void)stack_depth, (void)prio, (void)stack, (void)tcb;
  task_t* t = calloc(1, sizeof(*t));
  pthread_mutex_init(&t->m, NULL);
  pthread_cond_init(&t->c, NULL);
  t->fn = fn;
  t->arg = arg;
  pthread_t th;
  pthread_create(&th, NULL, task_main, t);
  pthread_detach(th);
  return t;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) {
  return s_self != NULL ? s_self : &s_main_task;  /* the test's own thread is a task too, never NULL */
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
  rb_item_t* head;     /* queued, oldest first (acquired or complete, not yet received) */
  rb_item_t* tail;
} rb_t;

RingbufHandle_t xRingbufferCreateStatic(size_t size, RingbufferType_t type, uint8_t* storage, StaticRingbuffer_t* st) {
  (void)type, (void)storage, (void)st;
  rb_t* rb = calloc(1, sizeof(*rb));
  pthread_mutex_init(&rb->m, NULL);
  pthread_cond_init(&rb->c, NULL);
  rb->cap = size;
  return rb;
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
  pthread_mutex_unlock(&rb->m);
  free(it);
}
