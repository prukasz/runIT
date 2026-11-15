/* Host tests of sys_event_stream with the real sys_event and data_matrix.
 * Run: python test_host/run.py sys_event_stream   (from runit-esp/) */
#include <pthread.h>
#include <string.h>
#include <time.h>

#include "data_matrix.h"
#include "host_assert.h"
#include "host_mocks.h"
#include "router.h"
#include "sys_event.h"
#include "sys_event_stream.h"
#include "sys_session.h"
#include "sys_session_internal.h"
#include "unity.h"

#define TEST_TAG EVT_TAG(0x7771, 1)

/* ---- the board's identity (board/ on the chip) ---------------------------------------------------------------- */

uint8_t sys_session_board_load_id(void) {
  return 5;
}
err_h sys_session_board_save_id(uint8_t id) {
  (void)id;
  return NULL;
}
uint32_t sys_session_board_boot_id(void) {
  return 1;
}
uint32_t sys_session_board_build_id(void) {
  return 2;
}

/* ---- what the matrix's callback got, and what the local listener got ------------------------------------------ */

static struct {
  pthread_mutex_t m;
  pthread_cond_t c;
  uint32_t out_count;
  sys_event_t out;  // the header of the last event that went out
  uint32_t in_count;
  sys_event_t in;  // the last event published here
} s_seen = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};

static void on_out(void* ctx, const data_matrix_end_t* from, const uint8_t* data, uint32_t len) {
  (void)ctx;
  (void)from;
  pthread_mutex_lock(&s_seen.m);
  if (len >= sizeof(s_seen.out)) {
    memcpy(&s_seen.out, data, sizeof(s_seen.out));
  }
  s_seen.out_count++;
  pthread_cond_broadcast(&s_seen.c);
  pthread_mutex_unlock(&s_seen.m);
}

static err_h on_published(const sys_event_t* event, void* ctx) {
  (void)ctx;
  pthread_mutex_lock(&s_seen.m);
  memcpy(&s_seen.in, event, sizeof(*event));
  s_seen.in_count++;
  pthread_cond_broadcast(&s_seen.c);
  pthread_mutex_unlock(&s_seen.m);
  return NULL;
}

static bool wait_for(const uint32_t* count, uint32_t n, int ms) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += ms / 1000;
  ts.tv_nsec += (long)(ms % 1000) * 1000000L;
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000L;
  }
  bool ok = true;
  pthread_mutex_lock(&s_seen.m);
  while (*count < n && ok) {
    ok = pthread_cond_timedwait(&s_seen.c, &s_seen.m, &ts) == 0;
  }
  ok = *count >= n;
  pthread_mutex_unlock(&s_seen.m);
  return ok;
}

void setUp(void) {
  static bool started;
  if (!started) {
    host_time_real(true);
    TEST_ASSERT_NULL(router_init());
    TEST_ASSERT_NULL(sys_event_init());
    TEST_ASSERT_NULL(sys_session_init());
    TEST_ASSERT_NULL(data_matrix_init());
    TEST_ASSERT_NULL(sys_event_stream_init());
    data_matrix_end_t out;
    TEST_ASSERT_NULL(data_matrix_listen(on_out, NULL, &out));
    TEST_ASSERT_NULL(data_matrix_route(&(data_matrix_route_t){DATA_MATRIX_END_STREAM(EVENT_STREAM_NOTIFY), out}));
    uint16_t handle = 0;
    TEST_ASSERT_NULL(sys_event_subscribe((sys_event_key_t){.tag = TEST_TAG, .source = SYS_EVENT_ANY_SOURCE,
                                                           .channel = SYS_EVENT_ANY_CHANNEL},
                                         on_published, NULL, SYS_EVENT_RUN_INLINE, &handle));
    started = true;
  }
}

void tearDown(void) {}

static void test_the_events_that_matter_go_out_on_the_stream(void) {
  pthread_mutex_lock(&s_seen.m);
  const uint32_t before = s_seen.out_count;
  pthread_mutex_unlock(&s_seen.m);
  const uint8_t payload[2] = {7, 8};
  TEST_ASSERT_NULL(sys_event_publish((sys_event_key_t){.tag = TEST_TAG, .source = 3, .channel = 4}, SYS_EVENT_NOTIFY | SYS_EVENT_DEFER,
                                     payload, sizeof(payload)));
  TEST_ASSERT_TRUE(wait_for(&s_seen.out_count, before + 1, 2000));
  pthread_mutex_lock(&s_seen.m);
  TEST_ASSERT_EQUAL_UINT32(TEST_TAG, s_seen.out.key.tag);
  TEST_ASSERT_EQUAL_UINT16(3, s_seen.out.key.source);
  TEST_ASSERT_EQUAL_UINT16(2, s_seen.out.len);
  pthread_mutex_unlock(&s_seen.m);
}

static void test_an_event_that_does_not_matter_stays_here(void) {
  pthread_mutex_lock(&s_seen.m);
  const uint32_t before = s_seen.out_count;
  pthread_mutex_unlock(&s_seen.m);
  TEST_ASSERT_NULL(sys_event_publish((sys_event_key_t){.tag = TEST_TAG, .source = 3, .channel = 4}, SYS_EVENT_DEFER, NULL, 0));
  TEST_ASSERT_FALSE(wait_for(&s_seen.out_count, before + 1, 200));
}

static void test_an_event_from_the_stream_is_published_without_notify(void) {
  pthread_mutex_lock(&s_seen.m);
  const uint32_t before = s_seen.in_count;
  pthread_mutex_unlock(&s_seen.m);
  uint8_t frame[sizeof(sys_event_t) + 1] = {0};
  sys_event_t event = {.key = {.tag = TEST_TAG, .source = 9, .channel = 1}, .flags = SYS_EVENT_NOTIFY, .len = 1};
  memcpy(frame, &event, sizeof(event));
  frame[sizeof(event)] = 0x55;
  TEST_ASSERT_NULL(data_matrix_send(EVENT_STREAM_REMOTE, frame, sizeof(frame)));
  // the stream has a route only once someone listens: sys_event_stream_init made the sink
  TEST_ASSERT_TRUE(wait_for(&s_seen.in_count, before + 1, 2000));
  pthread_mutex_lock(&s_seen.m);
  TEST_ASSERT_EQUAL_UINT16(9, s_seen.in.key.source);
  TEST_ASSERT_EQUAL_UINT16(0, s_seen.in.flags & SYS_EVENT_NOTIFY);  // not sent on again
  pthread_mutex_unlock(&s_seen.m);
}

static void test_a_torn_event_from_the_stream_is_dropped(void) {
  pthread_mutex_lock(&s_seen.m);
  const uint32_t before = s_seen.in_count;
  pthread_mutex_unlock(&s_seen.m);
  uint8_t frame[sizeof(sys_event_t)] = {0};
  sys_event_t event = {.key = {.tag = TEST_TAG, .source = 9, .channel = 1}, .len = 4};  // says 4 bytes, has none
  memcpy(frame, &event, sizeof(event));
  TEST_ASSERT_NULL(data_matrix_send(EVENT_STREAM_REMOTE, frame, sizeof(frame)));
  TEST_ASSERT_FALSE(wait_for(&s_seen.in_count, before + 1, 200));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_events_that_matter_go_out_on_the_stream);
  RUN_TEST(test_an_event_that_does_not_matter_stays_here);
  RUN_TEST(test_an_event_from_the_stream_is_published_without_notify);
  RUN_TEST(test_a_torn_event_from_the_stream_is_dropped);
  return UNITY_END();
}
