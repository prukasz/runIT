/* Host tests of the board's timers: the real code on the FreeRTOS stand-ins (the timers' task is a thread, a tick a
 * millisecond of the PC's clock). A test waits on a latch the callback gives; only "nothing more happened" waits a
 * short while. Run: python test_host/run.py timer   (runit-esp/test_host/HOST_TESTS.MD) */
#include "unity.h"
#include "board_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define LATCH_MS 1000u  // the longest a latch is waited for: far over any period here
#define QUIET_MS 40u    // "nothing more happened": several periods of the timers here

/* A callback's record: how many times it ran (read across threads), a latch given at each run and, when stop_at is
 * set, the timer it stops itself at that count. */
typedef struct {
  int calls;
  int stop_at;
  board_timer_t* self;
  SemaphoreHandle_t latch;
} probe_t;

static StaticSemaphore_t s_latch_buf[2];
static probe_t s_probe[2];
static board_timer_t s_timer[2];

static int calls(const probe_t* p) {
  return __atomic_load_n(&p->calls, __ATOMIC_SEQ_CST);
}

static void on_timer(void* ctx) {
  probe_t* p = ctx;
  const int n = __atomic_add_fetch(&p->calls, 1, __ATOMIC_SEQ_CST);
  if (p->stop_at != 0 && n >= p->stop_at) {
    board_timer_stop(p->self);
  }
  xSemaphoreGive(p->latch);
}

static bool latch(probe_t* p) {
  return xSemaphoreTake(p->latch, pdMS_TO_TICKS(LATCH_MS)) == pdTRUE;
}

void setUp(void) {
  for (int i = 0; i < 2; i++) {
    board_timer_stop(&s_timer[i]);
    vTaskDelay(pdMS_TO_TICKS(QUIET_MS));  // a call already started (the previous test's) ends
    if (s_probe[i].latch == NULL) {
      s_probe[i].latch = xSemaphoreCreateBinaryStatic(&s_latch_buf[i]);
    }
    while (xSemaphoreTake(s_probe[i].latch, 0) == pdTRUE) {
    }
    s_probe[i] = (probe_t){.self = &s_timer[i], .latch = s_probe[i].latch};
    board_timer_init(&s_timer[i], on_timer, &s_probe[i]);
  }
}

void tearDown(void) {
  board_timer_stop(&s_timer[0]);
  board_timer_stop(&s_timer[1]);
}

static void test_once_runs_once(void) {
  board_timer_once(&s_timer[0], 5);
  TEST_ASSERT_TRUE(board_timer_running(&s_timer[0]));
  TEST_ASSERT_TRUE(latch(&s_probe[0]));
  vTaskDelay(pdMS_TO_TICKS(QUIET_MS));
  TEST_ASSERT_EQUAL_INT(1, calls(&s_probe[0]));
  TEST_ASSERT_FALSE(board_timer_running(&s_timer[0]));
}

static void test_periodic_runs_until_stopped(void) {
  board_timer_start(&s_timer[0], 5);
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_TRUE(latch(&s_probe[0]));
  }
  board_timer_stop(&s_timer[0]);
  TEST_ASSERT_FALSE(board_timer_running(&s_timer[0]));
  vTaskDelay(pdMS_TO_TICKS(QUIET_MS));  // a call started before the stop ends
  const int after_stop = calls(&s_probe[0]);
  vTaskDelay(pdMS_TO_TICKS(QUIET_MS));
  TEST_ASSERT_EQUAL_INT(after_stop, calls(&s_probe[0]));
}

static void test_callback_stops_its_timer(void) {
  s_probe[0].stop_at = 3;
  board_timer_start(&s_timer[0], 2);
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_TRUE(latch(&s_probe[0]));
  }
  vTaskDelay(pdMS_TO_TICKS(QUIET_MS));
  TEST_ASSERT_EQUAL_INT(3, calls(&s_probe[0]));
}

// A new period starts the timer again from now: a 10 s one turned into 5 ms runs at once.
static void test_new_period_restarts(void) {
  board_timer_start(&s_timer[0], 10000);
  board_timer_start(&s_timer[0], 5);
  TEST_ASSERT_TRUE(latch(&s_probe[0]));
}

// The same period keeps the phase: starting again and again never lets a 30 ms timer run.
static void test_same_period_keeps_phase(void) {
  board_timer_start(&s_timer[0], 10000);
  for (int i = 0; i < 5; i++) {
    board_timer_start(&s_timer[0], 10000);
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  TEST_ASSERT_EQUAL_INT(0, calls(&s_probe[0]));
  TEST_ASSERT_TRUE(board_timer_running(&s_timer[0]));
}

static void test_two_timers(void) {
  board_timer_start(&s_timer[0], 3);
  board_timer_once(&s_timer[1], 7);
  TEST_ASSERT_TRUE(latch(&s_probe[1]));
  TEST_ASSERT_TRUE(latch(&s_probe[0]));
  TEST_ASSERT_TRUE(latch(&s_probe[0]));
  vTaskDelay(pdMS_TO_TICKS(QUIET_MS));
  TEST_ASSERT_EQUAL_INT(1, calls(&s_probe[1]));
}

static void test_stopped_once_never_runs(void) {
  board_timer_once(&s_timer[0], 20);
  board_timer_stop(&s_timer[0]);
  vTaskDelay(pdMS_TO_TICKS(QUIET_MS));
  TEST_ASSERT_EQUAL_INT(0, calls(&s_probe[0]));
}

// A callback slower than its period (a bus that hangs): its timer is due again each time it ends.
static void on_slow(void* ctx) {
  on_timer(ctx);
  vTaskDelay(pdMS_TO_TICKS(15));
}

/* The timer that waited longest runs first: a slow timer, due again after every call of its own, still lets the other
 * one run. It is started last, so it is first in the list. */
static void test_slow_timer_does_not_starve_another(void) {
  board_timer_start(&s_timer[1], 5);
  board_timer_init(&s_timer[0], on_slow, &s_probe[0]);
  board_timer_start(&s_timer[0], 5);
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_TRUE(latch(&s_probe[1]));
  }
  TEST_ASSERT_TRUE(calls(&s_probe[0]) >= 2);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_once_runs_once);
  RUN_TEST(test_periodic_runs_until_stopped);
  RUN_TEST(test_callback_stops_its_timer);
  RUN_TEST(test_new_period_restarts);
  RUN_TEST(test_same_period_keeps_phase);
  RUN_TEST(test_two_timers);
  RUN_TEST(test_stopped_once_never_runs);
  RUN_TEST(test_slow_timer_does_not_starve_another);
  return UNITY_END();
}
