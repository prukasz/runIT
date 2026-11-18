/* Host tests of sys_event: the real sys_event.c and errors; only the platform is replaced (tasks are threads, an ISR is
 * a thread that says it is one: host_isr_set). The tests up to the queue's are run without the lanes' tasks; the ones
 * after it start them (sys_event_init) and they stay: a test there waits for what a task delivers.
 * Run: python test_host/run.py sys_event   (runit-esp/test_host/HOST_TESTS.MD) */
#include <pthread.h>
#include <string.h>
#include "err_sink.h"
#include "module_id.h"
#include "unity.h"
#include "host_assert.h"
#include "host_mocks.h"
#include "utils.h"
#include "sys_event.h"

/* A module of the tests' own, with a table in the format every module uses. */
MODULE_ID_DECLARE(MODULE_ID_TESTEV, 0x3A)
#define TESTEV_EVENTS(X, P)                              \
  X(P, PING, 0x01, 0,                {})                 \
  X(P, ODD,  0x02, SYS_EVENT_NOTIFY, { uint8_t b[3]; })  \
  X(P, WORD, 0x03, 0,                { uint32_t v; })
SYS_EVENTS_DECLARE(TESTEV, MODULE_ID_TESTEV, SYS)

/* A second one, in the other lane. */
MODULE_ID_DECLARE(MODULE_ID_TESTDEV, 0x3C)
#define TESTDEV_EVENTS(X, P) X(P, BEEP, 0x01, 0, { uint32_t v; })
SYS_EVENTS_DECLARE(TESTDEV, MODULE_ID_TESTDEV, DEVICE)

#define WAIT_MS 1000  // the longest a test waits for a lane's task

/* ---- what the listeners saw (any task) ------------------------------------------------------------------------ */

#define REC_MAX 64
typedef struct {
  uint32_t tag;
  uint16_t source;
  uint16_t channel;
  uint16_t flags;
  uint16_t len;
  uint64_t t_ms;
  uint8_t data[8];
  TaskHandle_t task;
  int who;  // the listener's ctx
} seen_t;

static pthread_mutex_t s_rec_lock = PTHREAD_MUTEX_INITIALIZER;
static seen_t s_rec[REC_MAX];
static int s_rec_n;

static int rec_count(void) {
  pthread_mutex_lock(&s_rec_lock);
  const int n = s_rec_n;
  pthread_mutex_unlock(&s_rec_lock);
  return n;
}

// Waits until the listeners saw `n` events; false after WAIT_MS.
static bool wait_rec(int n) {
  for (int i = 0; i < WAIT_MS && rec_count() < n; i++) {
    vTaskDelay(1);
  }
  return rec_count() >= n;
}

static err_h recorder(const sys_event_t* e, void* ctx) {
  pthread_mutex_lock(&s_rec_lock);
  if (s_rec_n < REC_MAX) {
    seen_t* s = &s_rec[s_rec_n++];
    *s = (seen_t){.tag = e->key.tag, .source = e->key.source, .channel = e->key.channel, .flags = e->flags,
                  .len = e->len, .t_ms = e->t_ms, .task = xTaskGetCurrentTaskHandle(), .who = (int)(intptr_t)ctx};
    memcpy(s->data, e->data, e->len < sizeof(s->data) ? e->len : sizeof(s->data));
  }
  pthread_mutex_unlock(&s_rec_lock);
  return NULL;
}

static err_h failing(const sys_event_t* e, void* ctx) {
  (void)e, (void)ctx;
  return ERR_NEW(ERR_EVENT_BAD_ARG);
}

// A PING from source 1 makes it publish a WORD from source 9, inline (the hold is over when it runs).
static err_h republisher(const sys_event_t* e, void* ctx) {
  (void)ctx;
  if (e->key.tag == TESTEV_EV_PING && e->key.source == 1) {
    return SYS_EVENT_PUBLISH(TESTEV_EV_WORD, 9, 0, .v = 0x99);
  }
  return NULL;
}

/* ---- what reached the error sink (any task) ------------------------------------------------------------------- */

static pthread_mutex_t s_sink_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_sink_n;
static uint32_t s_sink_outer;
static uint32_t s_sink_root;
static uint32_t s_sink_dropped;
static uint16_t s_sink_source;
static uint16_t s_sink_channel;

static void sink(err_h chain, const struct cmd_req* cmd) {
  (void)cmd;
  pthread_mutex_lock(&s_sink_lock);
  s_sink_n++;
  s_sink_outer = err_tag(chain);
  s_sink_root = err_root_tag(chain);
  if (err_tag(chain) == ERR_EVENT_QUEUE_FULL) {
    s_sink_dropped += ERR_DATA(chain, ERR_EVENT_QUEUE_FULL)->dropped;
  }
  if (err_tag(chain) == ERR_EVENT_DELIVERY) {
    s_sink_source = ERR_DATA(chain, ERR_EVENT_DELIVERY)->source;
    s_sink_channel = ERR_DATA(chain, ERR_EVENT_DELIVERY)->channel;
  }
  pthread_mutex_unlock(&s_sink_lock);
}

static int sink_count(void) {
  pthread_mutex_lock(&s_sink_lock);
  const int n = s_sink_n;
  pthread_mutex_unlock(&s_sink_lock);
  return n;
}

/* ---- holds: static, so tearDown ends one a failed assertion left on (the task's hold must not point to a gone
 * stack frame) ---------------------------------------------------------------------------------------------------- */

static uint32_t s_room[64];
static uint32_t s_room2[32];
static sys_event_hold_t s_hold;
static sys_event_hold_t s_hold2;

/* ---- subscriptions made by a test, removed after it ----------------------------------------------------------- */

static uint16_t s_handles[CONFIG_SYS_EVENT_MAX_SUBS + 8];
static int s_handle_n;

static sys_event_key_t key(uint32_t tag, uint16_t source, uint16_t channel) {
  return (sys_event_key_t){.tag = tag, .source = source, .channel = channel};
}

static uint16_t sub(sys_event_key_t filter, sys_event_handler_f fn, int who, sys_event_run_e run) {
  uint16_t h = 0;
  TEST_ASSERT_NULL(sys_event_subscribe(filter, fn, (void*)(intptr_t)who, run, &h));
  s_handles[s_handle_n++] = h;
  return h;
}

void setUp(void) {
  host_log_clear();
  host_time_set(0);
  err_set_sink(sink);
  pthread_mutex_lock(&s_rec_lock);
  s_rec_n = 0;
  pthread_mutex_unlock(&s_rec_lock);
  pthread_mutex_lock(&s_sink_lock);
  s_sink_n = 0;
  s_sink_dropped = 0;
  pthread_mutex_unlock(&s_sink_lock);
}

void tearDown(void) {
  sys_event_release(&s_hold2);  // the inner one first: a no-op unless a test stopped inside it
  sys_event_release(&s_hold);
  for (int i = 0; i < s_handle_n; i++) {
    err_free(sys_event_unsubscribe(s_handles[i]));  // one a test removed itself: NOT_FOUND, freed
  }
  s_handle_n = 0;
  host_isr_set(0);
}

/* ---- the events table ----------------------------------------------------------------------------------------- */

static void test_a_table_takes_its_module_from_its_prefix(void) {
  TEST_ASSERT_EQUAL_HEX16(0x3A, TESTEV_EV_MODULE);
  TEST_ASSERT_EQUAL_HEX32(EVT_TAG(0x3A, 0x01), TESTEV_EV_PING);
  TEST_ASSERT_EQUAL_HEX32(0x003A0002u, TESTEV_EV_ODD);
  TEST_ASSERT_EQUAL_HEX16(SYS_EVENT_NOTIFY, TESTEV_EV_ODD_FLAGS);
  TEST_ASSERT_EQUAL_HEX16(0, TESTEV_EV_PING_FLAGS);
  TEST_ASSERT_EQUAL_UINT32(3, sizeof(evt_data_TESTEV_EV_ODD_t));
  TEST_ASSERT_EQUAL_UINT32(0, sizeof(evt_data_TESTEV_EV_PING_t));
}

static void test_a_table_puts_its_lane_into_its_events_flags(void) {
  TEST_ASSERT_EQUAL_INT(SYS_EVENT_LANE_SYS, TESTEV_EV_LANE);
  TEST_ASSERT_EQUAL_INT(SYS_EVENT_LANE_DEVICE, TESTDEV_EV_LANE);
  TEST_ASSERT_EQUAL_HEX16(SYS_EVENT_FLAG_LANE(SYS_EVENT_LANE_DEVICE), TESTDEV_EV_BEEP_FLAGS);
  TEST_ASSERT_EQUAL_UINT32(SYS_EVENT_LANE_DEVICE, SYS_EVENT_LANE_OF(TESTDEV_EV_BEEP_FLAGS));
  TEST_ASSERT_EQUAL_UINT32(SYS_EVENT_LANE_SYS, SYS_EVENT_LANE_OF(TESTEV_EV_ODD_FLAGS));
  TEST_ASSERT_EQUAL_INT(SYS_EVENT_RUN_QUEUED_DEVICE, SYS_EVENT_RUN_QUEUED_IN(SYS_EVENT_LANE_DEVICE));
}

/* ---- publish and the inline listeners ------------------------------------------------------------------------- */

static void test_an_inline_listener_runs_before_publish_returns(void) {
  sub(key(TESTEV_EV_PING, SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_CHANNEL), recorder, 1, SYS_EVENT_RUN_INLINE);
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_WORD, 5, 0, .v = 1));  // another tag: not this listener's
  TEST_ASSERT_EQUAL_INT(0, rec_count());
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_PING, 5, 2));
  TEST_ASSERT_EQUAL_INT(1, rec_count());
  TEST_ASSERT_EQUAL_HEX32(TESTEV_EV_PING, s_rec[0].tag);
  TEST_ASSERT_EQUAL_UINT16(5, s_rec[0].source);
  TEST_ASSERT_EQUAL_UINT16(2, s_rec[0].channel);
  TEST_ASSERT_EQUAL_PTR(xTaskGetCurrentTaskHandle(), s_rec[0].task);
}

static void test_publish_stamps_the_clock_the_tables_flags_and_the_payload(void) {
  sub(key(SYS_EVENT_ANY_TAG, SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_CHANNEL), recorder, 1, SYS_EVENT_RUN_INLINE);
  host_time_set(1234);
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_ODD, 3, 0, .b = {1, 2, 3}));
  TEST_ASSERT_EQUAL_INT(1, rec_count());
  TEST_ASSERT_EQUAL_UINT64(1234, s_rec[0].t_ms);
  TEST_ASSERT_EQUAL_HEX16(SYS_EVENT_NOTIFY, s_rec[0].flags);
  TEST_ASSERT_EQUAL_UINT16(3, s_rec[0].len);
  const uint8_t b[] = {1, 2, 3};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(b, s_rec[0].data, 3);
}

static void test_a_filter_matches_each_field_exact_or_any(void) {
  uint32_t buf[8] = {0};
  sys_event_t* e = (sys_event_t*)buf;
  e->key = key(TESTEV_EV_ODD, 4, 7);
  e->flags = SYS_EVENT_NOTIFY;
  const sys_event_key_t yes[] = {
      key(TESTEV_EV_ODD, 4, 7),
      key(TESTEV_EV_ODD, SYS_EVENT_ANY_SOURCE, 7),
      key(TESTEV_EV_ODD, 4, SYS_EVENT_ANY_CHANNEL),
      key(EVT_TAG(0x3A, SYS_EVENT_ANY_CODE), 4, 7),
      key(SYS_EVENT_ANY_TAG, SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_CHANNEL),
      key(SYS_EVENT_ANY_NOTIFY, SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_CHANNEL),
  };
  const sys_event_key_t no[] = {
      key(TESTEV_EV_PING, 4, 7),
      key(TESTEV_EV_ODD, 5, 7),
      key(TESTEV_EV_ODD, 4, 8),
      key(EVT_TAG(0x3B, SYS_EVENT_ANY_CODE), 4, 7),
  };
  for (unsigned i = 0; i < ARRAY_LEN(yes); i++) {
    TEST_ASSERT_TRUE_MESSAGE(sys_event_match(&yes[i], e), "a filter that should match");
  }
  for (unsigned i = 0; i < ARRAY_LEN(no); i++) {
    TEST_ASSERT_FALSE_MESSAGE(sys_event_match(&no[i], e), "a filter that should not match");
  }
  e->flags = 0;
  const sys_event_key_t notify = key(SYS_EVENT_ANY_NOTIFY, SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_CHANNEL);
  TEST_ASSERT_FALSE(sys_event_match(&notify, e));
}

static void test_a_payload_too_long_or_missing_is_refused(void) {
  sub(key(SYS_EVENT_ANY_TAG, SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_CHANNEL), recorder, 1, SYS_EVENT_RUN_INLINE);
  uint8_t big[SYS_EVENT_DATA_MAX + 1] = {0};
  err_h e = sys_event_publish(key(TESTEV_EV_WORD, 1, 0), 0, big, sizeof(big));
  TEST_ASSERT_ERR_TAG(ERR_EVENT_BAD_ARG, e);
  err_free(e);
  e = sys_event_publish(key(TESTEV_EV_WORD, 1, 0), 0, NULL, 4);
  TEST_ASSERT_ERR_TAG(ERR_EVENT_BAD_ARG, e);
  err_free(e);
  e = sys_event_publish(key(TESTEV_EV_PING, 1, 0), SYS_EVENT_FLAG_LANE(SYS_EVENT_LANE_COUNT), NULL, 0);  // no lane
  TEST_ASSERT_ERR_TAG(ERR_EVENT_BAD_ARG, e);
  err_free(e);
  TEST_ASSERT_EQUAL_INT(0, rec_count());
}

static void test_a_failing_listener_is_reported_with_the_event_and_the_others_still_run(void) {
  sub(key(TESTEV_EV_PING, SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_CHANNEL), failing, 1, SYS_EVENT_RUN_INLINE);
  sub(key(TESTEV_EV_PING, SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_CHANNEL), recorder, 2, SYS_EVENT_RUN_INLINE);
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_PING, 7, 2));  // the listener's failure is not the publisher's
  TEST_ASSERT_EQUAL_INT(1, rec_count());
  TEST_ASSERT_EQUAL_INT(1, sink_count());
  TEST_ASSERT_EQUAL_HEX32(ERR_EVENT_DELIVERY, s_sink_outer);
  TEST_ASSERT_EQUAL_HEX32(ERR_EVENT_BAD_ARG, s_sink_root);
  TEST_ASSERT_EQUAL_UINT16(7, s_sink_source);
  TEST_ASSERT_EQUAL_UINT16(2, s_sink_channel);
}

/* ---- subscriptions -------------------------------------------------------------------------------------------- */

static void test_run_first_takes_the_first_slot_and_runs_before_the_others(void) {
  sub(key(TESTEV_EV_PING, SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_CHANNEL), recorder, 1, SYS_EVENT_RUN_INLINE);
  sub(key(TESTEV_EV_PING, SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_CHANNEL), recorder, 2, SYS_EVENT_RUN_FIRST);
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_PING, 1, 0));
  TEST_ASSERT_EQUAL_INT(2, rec_count());
  TEST_ASSERT_EQUAL_INT(2, s_rec[0].who);  // subscribed later, ran first
  TEST_ASSERT_EQUAL_INT(1, s_rec[1].who);
  uint16_t h = 0;
  err_h e = sys_event_subscribe(key(SYS_EVENT_ANY_TAG, 0, 0), recorder, NULL, SYS_EVENT_RUN_FIRST, &h);
  TEST_ASSERT_ERR_TAG(ERR_EVENT_TABLE_FULL, e);  // one first listener
  err_free(e);
}

static void test_a_full_table_refuses_one_more_and_bad_arguments_are_refused(void) {
  int added = 0;
  err_h e = NULL;
  while (e == NULL && added < CONFIG_SYS_EVENT_MAX_SUBS + 4) {
    uint16_t h = 0;
    e = sys_event_subscribe(key(TESTEV_EV_PING, 0, 0), recorder, NULL, SYS_EVENT_RUN_INLINE, &h);
    if (e == NULL) {
      s_handles[s_handle_n++] = h;
      added++;
    }
  }
  TEST_ASSERT_EQUAL_INT(CONFIG_SYS_EVENT_MAX_SUBS - 1, added);  // slot 0 is the first listener's
  TEST_ASSERT_ERR_TAG(ERR_EVENT_TABLE_FULL, e);
  err_free(e);
  uint16_t h = 0;
  e = sys_event_subscribe(key(TESTEV_EV_PING, 0, 0), NULL, NULL, SYS_EVENT_RUN_INLINE, &h);
  TEST_ASSERT_ERR_TAG(ERR_EVENT_BAD_ARG, e);
  err_free(e);
  e = sys_event_subscribe(key(TESTEV_EV_PING, 0, 0), recorder, NULL, SYS_EVENT_RUN_INLINE, NULL);
  TEST_ASSERT_ERR_TAG(ERR_EVENT_BAD_ARG, e);
  err_free(e);
  e = sys_event_subscribe(key(TESTEV_EV_PING, 0, 0), recorder, NULL, SYS_EVENT_RUN_QUEUED_IN(SYS_EVENT_LANE_COUNT), &h);
  TEST_ASSERT_ERR_TAG(ERR_EVENT_BAD_ARG, e);  // no such lane
  err_free(e);
}

static void test_a_handle_names_only_its_own_listener(void) {
  err_h e = sys_event_unsubscribe(0);
  TEST_ASSERT_ERR_TAG(ERR_EVENT_NOT_FOUND, e);
  TEST_ASSERT_EQUAL_UINT16(0, ERR_DATA(e, ERR_EVENT_NOT_FOUND)->id);
  err_free(e);
  uint16_t old = 0;
  TEST_ASSERT_NULL(sys_event_subscribe(key(TESTEV_EV_PING, 0, 0), recorder, NULL, SYS_EVENT_RUN_INLINE, &old));
  TEST_ASSERT_NULL(sys_event_unsubscribe(old));
  e = sys_event_unsubscribe(old);  // removed already
  TEST_ASSERT_ERR_TAG(ERR_EVENT_NOT_FOUND, e);
  err_free(e);
  const uint16_t now = sub(key(TESTEV_EV_PING, 0, 0), recorder, 1, SYS_EVENT_RUN_INLINE);
  TEST_ASSERT_EQUAL_HEX8(old & 0xFFu, now & 0xFFu);  // the same slot
  TEST_ASSERT_NOT_EQUAL(old, now);                   // another handle
  e = sys_event_unsubscribe(old);                    // the old handle does not remove the new listener
  TEST_ASSERT_ERR_TAG(ERR_EVENT_NOT_FOUND, e);
  err_free(e);
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_PING, 0, 0));
  TEST_ASSERT_EQUAL_INT(1, rec_count());
}

static void test_a_handles_generation_goes_back_to_1_never_to_0(void) {
  uint16_t first = 0;
  for (int i = 0; i < 300; i++) {
    uint16_t h = 0;
    TEST_ASSERT_NULL(sys_event_subscribe(key(TESTEV_EV_PING, 0, 0), recorder, NULL, SYS_EVENT_RUN_INLINE, &h));
    TEST_ASSERT_NOT_EQUAL(0, h);
    TEST_ASSERT_NOT_EQUAL(0, h >> 8);  // the generation
    if (i == 0) {
      first = h;
    }
    TEST_ASSERT_EQUAL_HEX8(first & 0xFFu, h & 0xFFu);
    TEST_ASSERT_NULL(sys_event_unsubscribe(h));
  }
}

/* ---- holding a task's events ---------------------------------------------------------------------------------- */

static void test_a_hold_keeps_the_events_until_its_release_in_order_and_with_their_time(void) {
  sub(key(SYS_EVENT_ANY_TAG, SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_CHANNEL), recorder, 1, SYS_EVENT_RUN_INLINE);
  sys_event_hold(&s_hold, s_room, sizeof(s_room));
  host_time_set(100);
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_ODD, 1, 0, .b = {7, 8, 9}));  // 3 B: the next one starts padded
  host_time_set(200);
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_WORD, 2, 0, .v = 0xA1B2C3D4u));
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_PING, 3, 0));
  TEST_ASSERT_EQUAL_INT(0, rec_count());
  host_time_set(999);
  sys_event_release(&s_hold);
  TEST_ASSERT_EQUAL_INT(3, rec_count());
  TEST_ASSERT_EQUAL_HEX32(TESTEV_EV_ODD, s_rec[0].tag);
  TEST_ASSERT_EQUAL_HEX32(TESTEV_EV_WORD, s_rec[1].tag);
  TEST_ASSERT_EQUAL_HEX32(TESTEV_EV_PING, s_rec[2].tag);
  TEST_ASSERT_EQUAL_UINT64(100, s_rec[0].t_ms);  // the time of the publish, not of the release
  TEST_ASSERT_EQUAL_UINT64(200, s_rec[1].t_ms);
  const uint8_t odd[] = {7, 8, 9};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(odd, s_rec[0].data, 3);
  uint32_t v;
  memcpy(&v, s_rec[1].data, sizeof(v));
  TEST_ASSERT_EQUAL_HEX32(0xA1B2C3D4u, v);
  TEST_ASSERT_EQUAL_UINT16(3, s_rec[2].source);
  sys_event_release(&s_hold);  // released twice: nothing
  TEST_ASSERT_EQUAL_INT(3, rec_count());
}

static void test_a_hold_inside_a_hold_keeps_nothing(void) {
  sub(key(TESTEV_EV_PING, SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_CHANNEL), recorder, 1, SYS_EVENT_RUN_INLINE);
  sys_event_hold(&s_hold, s_room, sizeof(s_room));
  sys_event_hold(&s_hold2, s_room2, sizeof(s_room2));
  TEST_ASSERT_NULL(s_hold2.buf);
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_PING, 1, 0));
  sys_event_release(&s_hold2);
  TEST_ASSERT_EQUAL_INT(0, rec_count());  // the outer hold keeps it
  sys_event_release(&s_hold);
  TEST_ASSERT_EQUAL_INT(1, rec_count());
}

static void test_a_listener_may_publish_while_a_hold_is_released(void) {
  sub(key(SYS_EVENT_ANY_TAG, SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_CHANNEL), recorder, 1, SYS_EVENT_RUN_INLINE);
  sub(key(TESTEV_EV_PING, SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_CHANNEL), republisher, 2, SYS_EVENT_RUN_INLINE);
  sys_event_hold(&s_hold, s_room, sizeof(s_room));
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_PING, 1, 0));
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_ODD, 2, 0, .b = {4, 5, 6}));
  sys_event_release(&s_hold);
  TEST_ASSERT_EQUAL_INT(3, rec_count());
  TEST_ASSERT_EQUAL_HEX32(TESTEV_EV_PING, s_rec[0].tag);
  TEST_ASSERT_EQUAL_HEX32(TESTEV_EV_WORD, s_rec[1].tag);  // the listener's, delivered at once
  TEST_ASSERT_EQUAL_UINT16(9, s_rec[1].source);
  TEST_ASSERT_EQUAL_HEX32(TESTEV_EV_ODD, s_rec[2].tag);   // the held ones read on undamaged
  const uint8_t odd[] = {4, 5, 6};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(odd, s_rec[2].data, 3);
}

/* ---- the queue, before the event task runs: it only fills ----------------------------------------------------- */

static void test_without_the_event_task_the_queue_fills_and_says_so(void) {
  sub(key(TESTEV_EV_PING, 0x77, SYS_EVENT_ANY_CHANNEL), recorder, 1, SYS_EVENT_RUN_QUEUED_SYS);
  err_h e = NULL;
  int queued = 0;
  while (e == NULL && queued < 1000) {
    e = SYS_EVENT_PUBLISH(TESTEV_EV_PING, 0x77, 0);
    queued += e == NULL ? 1 : 0;
  }
  TEST_ASSERT_TRUE(queued > 5 && queued < 1000);
  TEST_ASSERT_ERR_TAG(ERR_EVENT_QUEUE_FULL, e);
  TEST_ASSERT_EQUAL_UINT16(0x77, ERR_DATA(e, ERR_EVENT_QUEUE_FULL)->source);
  TEST_ASSERT_EQUAL_HEX32(TESTEV_EV_PING, ERR_DATA(e, ERR_EVENT_QUEUE_FULL)->event);
  TEST_ASSERT_EQUAL_UINT32(1, ERR_DATA(e, ERR_EVENT_QUEUE_FULL)->dropped);
  err_free(e);
  // A held event's queued copy that does not fit is reported at the release, not lost silently.
  sys_event_hold(&s_hold, s_room, sizeof(s_room));
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_PING, 0x77, 0));
  sys_event_release(&s_hold);
  TEST_ASSERT_EQUAL_INT(1, sink_count());
  TEST_ASSERT_EQUAL_HEX32(ERR_EVENT_QUEUE_FULL, s_sink_outer);
  TEST_ASSERT_EQUAL_INT(0, rec_count());  // no task: nothing was delivered
  // The other lane has its own ring: the full one does not stop it.
  sub(key(TESTDEV_EV_BEEP, 0x77, SYS_EVENT_ANY_CHANNEL), recorder, 2, SYS_EVENT_RUN_QUEUED_DEVICE);
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTDEV_EV_BEEP, 0x77, 0, .v = 1));
}

/* ---- the lanes' tasks (started here and kept) ----------------------------------------------------------------- */

/* Waits until the SYS lane's task has taken everything queued before: a marker (source 0x7E) is queued, retried while
 * the ring is full, and the ring is a FIFO, so when the marker's queued listener has it, nothing older is left. The
 * marker's record is then forgotten. (The DEVICE lane holds one BEEP of source 0x77 from the test before: no
 * listener of a later test takes that source.) */
static void drain_queue(void) {
  sub(key(TESTEV_EV_PING, 0x7E, SYS_EVENT_ANY_CHANNEL), recorder, 0x7E, SYS_EVENT_RUN_QUEUED_SYS);
  const int before = rec_count();
  err_h e = SYS_EVENT_PUBLISH(TESTEV_EV_PING, 0x7E, 0);
  for (int i = 0; i < WAIT_MS && e != NULL; i++) {
    err_free(e);
    vTaskDelay(1);
    e = SYS_EVENT_PUBLISH(TESTEV_EV_PING, 0x7E, 0);
  }
  TEST_ASSERT_NULL(e);
  TEST_ASSERT_TRUE(wait_rec(before + 1));
  pthread_mutex_lock(&s_rec_lock);
  s_rec_n = before;
  pthread_mutex_unlock(&s_rec_lock);
}

static void test_the_event_task_gives_queued_listeners_a_copy(void) {
  TEST_ASSERT_NULL(sys_event_init());
  drain_queue();  // what the test before left, to no listener
  sub(key(SYS_EVENT_ANY_TAG, 0x10, SYS_EVENT_ANY_CHANNEL), recorder, 1, SYS_EVENT_RUN_QUEUED_SYS);
  sub(key(SYS_EVENT_ANY_TAG, 0x10, SYS_EVENT_ANY_CHANNEL), recorder, 2, SYS_EVENT_RUN_INLINE);
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_WORD, 0x10, 0, .v = 7));
  TEST_ASSERT_TRUE(wait_rec(2));
  int queued = s_rec[0].who == 1 ? 0 : 1;
  TEST_ASSERT_EQUAL_INT(2, s_rec[1 - queued].who);  // the inline one, in this task
  TEST_ASSERT_EQUAL_PTR(xTaskGetCurrentTaskHandle(), s_rec[1 - queued].task);
  TEST_ASSERT_NOT_EQUAL(xTaskGetCurrentTaskHandle(), s_rec[queued].task);
  uint32_t v;
  memcpy(&v, s_rec[queued].data, sizeof(v));
  TEST_ASSERT_EQUAL_UINT32(7, v);
  // A held event reaches its queued listener too.
  sys_event_hold(&s_hold, s_room, sizeof(s_room));
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_PING, 0x10, 0));
  sys_event_release(&s_hold);
  TEST_ASSERT_TRUE(wait_rec(4));
}

static void test_a_queued_listener_hears_only_its_lane_in_its_lanes_task(void) {
  sub(key(SYS_EVENT_ANY_TAG, 0x15, SYS_EVENT_ANY_CHANNEL), recorder, 1, SYS_EVENT_RUN_QUEUED_SYS);
  sub(key(SYS_EVENT_ANY_TAG, 0x15, SYS_EVENT_ANY_CHANNEL), recorder, 2, SYS_EVENT_RUN_QUEUED_DEVICE);
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_PING, 0x15, 0));
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTDEV_EV_BEEP, 0x15, 0, .v = 2));
  TEST_ASSERT_TRUE(wait_rec(2));
  vTaskDelay(20);
  TEST_ASSERT_EQUAL_INT(2, rec_count());  // each event once: to the listener of its lane
  const int ping = s_rec[0].tag == TESTEV_EV_PING ? 0 : 1;
  TEST_ASSERT_EQUAL_HEX32(TESTEV_EV_PING, s_rec[ping].tag);
  TEST_ASSERT_EQUAL_INT(1, s_rec[ping].who);
  TEST_ASSERT_EQUAL_HEX32(TESTDEV_EV_BEEP, s_rec[1 - ping].tag);
  TEST_ASSERT_EQUAL_INT(2, s_rec[1 - ping].who);
  TEST_ASSERT_EQUAL_HEX16(TESTDEV_EV_BEEP_FLAGS, s_rec[1 - ping].flags);  // the lane travels in the event
  TEST_ASSERT_NOT_EQUAL(s_rec[0].task, s_rec[1].task);                    // two tasks
  TEST_ASSERT_NOT_EQUAL(xTaskGetCurrentTaskHandle(), s_rec[0].task);
  TEST_ASSERT_NOT_EQUAL(xTaskGetCurrentTaskHandle(), s_rec[1].task);
}

static void test_a_deferred_publish_runs_the_inline_listeners_in_the_event_task(void) {
  sub(key(SYS_EVENT_ANY_TAG, 0x11, SYS_EVENT_ANY_CHANNEL), recorder, 1, SYS_EVENT_RUN_INLINE);
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH_DEFER(TESTEV_EV_PING, 0x11, 0));
  TEST_ASSERT_TRUE(wait_rec(1));
  TEST_ASSERT_NOT_EQUAL(xTaskGetCurrentTaskHandle(), s_rec[0].task);
  TEST_ASSERT_EQUAL_HEX16(0, s_rec[0].flags);  // DEFER says how, it is not in the event
}

static void test_an_event_that_does_not_fit_the_hold_goes_to_the_event_task(void) {
  sub(key(SYS_EVENT_ANY_TAG, 0x12, SYS_EVENT_ANY_CHANNEL), recorder, 1, SYS_EVENT_RUN_INLINE);
  sys_event_hold(&s_hold, s_room, 8);  // 8 B: no event fits
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_PING, 0x12, 0));
  TEST_ASSERT_TRUE(wait_rec(1));  // while the hold is still on
  TEST_ASSERT_NOT_EQUAL(xTaskGetCurrentTaskHandle(), s_rec[0].task);
  sys_event_release(&s_hold);
  vTaskDelay(20);
  TEST_ASSERT_EQUAL_INT(1, rec_count());  // once
}

static void test_a_publish_from_an_isr_is_deferred_and_a_drop_is_reported_by_the_task(void) {
  sub(key(SYS_EVENT_ANY_TAG, 0x13, SYS_EVENT_ANY_CHANNEL), recorder, 1, SYS_EVENT_RUN_INLINE);
  uint8_t big[SYS_EVENT_DATA_MAX + 1] = {0};
  host_time_set(500);
  host_isr_set(1);
  TEST_ASSERT_NULL(sys_event_publish(key(TESTEV_EV_WORD, 0x13, 0), 0, big, sizeof(big)));  // dropped, not returned
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_PING, 0x13, 0));
  host_isr_set(0);
  TEST_ASSERT_TRUE(wait_rec(1));
  TEST_ASSERT_NOT_EQUAL(xTaskGetCurrentTaskHandle(), s_rec[0].task);
  TEST_ASSERT_EQUAL_UINT64(500, s_rec[0].t_ms);  // the uptime read in the ISR, made the clock by the task
  for (int i = 0; i < WAIT_MS && sink_count() == 0; i++) {
    vTaskDelay(1);
  }
  TEST_ASSERT_EQUAL_INT(1, sink_count());
  TEST_ASSERT_EQUAL_HEX32(ERR_EVENT_QUEUE_FULL, s_sink_outer);
  TEST_ASSERT_EQUAL_UINT32(1, s_sink_dropped);
}

static SemaphoreHandle_t s_done;
static StaticSemaphore_t s_done_buf;

static void publisher_task(void* arg) {
  (void)arg;
  err_report(SYS_EVENT_PUBLISH(TESTEV_EV_PING, 0x14, 1));
  xSemaphoreGive(s_done);
  while (1) {
    vTaskDelay(1000);
  }
}

static void test_a_hold_of_one_task_does_not_hold_another(void) {
  sub(key(SYS_EVENT_ANY_TAG, 0x14, SYS_EVENT_ANY_CHANNEL), recorder, 1, SYS_EVENT_RUN_INLINE);
  s_done = xSemaphoreCreateBinaryStatic(&s_done_buf);
  sys_event_hold(&s_hold, s_room, sizeof(s_room));
  TaskHandle_t other = xTaskCreateStatic(publisher_task, "pub", 0, NULL, 1, NULL, NULL);
  TEST_ASSERT_TRUE(xSemaphoreTake(s_done, WAIT_MS) == pdTRUE);
  TEST_ASSERT_EQUAL_INT(1, rec_count());  // delivered in the other task, while this one holds
  TEST_ASSERT_EQUAL_PTR(other, s_rec[0].task);
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(TESTEV_EV_PING, 0x14, 0));
  TEST_ASSERT_EQUAL_INT(1, rec_count());  // this one's is held
  sys_event_release(&s_hold);
  TEST_ASSERT_EQUAL_INT(2, rec_count());
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);  // every line out before a crash
  UNITY_BEGIN();
  RUN_TEST(test_a_table_takes_its_module_from_its_prefix);
  RUN_TEST(test_a_table_puts_its_lane_into_its_events_flags);
  RUN_TEST(test_an_inline_listener_runs_before_publish_returns);
  RUN_TEST(test_publish_stamps_the_clock_the_tables_flags_and_the_payload);
  RUN_TEST(test_a_filter_matches_each_field_exact_or_any);
  RUN_TEST(test_a_payload_too_long_or_missing_is_refused);
  RUN_TEST(test_a_failing_listener_is_reported_with_the_event_and_the_others_still_run);
  RUN_TEST(test_run_first_takes_the_first_slot_and_runs_before_the_others);
  RUN_TEST(test_a_full_table_refuses_one_more_and_bad_arguments_are_refused);
  RUN_TEST(test_a_handle_names_only_its_own_listener);
  RUN_TEST(test_a_handles_generation_goes_back_to_1_never_to_0);
  RUN_TEST(test_a_hold_keeps_the_events_until_its_release_in_order_and_with_their_time);
  RUN_TEST(test_a_hold_inside_a_hold_keeps_nothing);
  RUN_TEST(test_a_listener_may_publish_while_a_hold_is_released);
  RUN_TEST(test_without_the_event_task_the_queue_fills_and_says_so);  // before the task: the queue only fills
  RUN_TEST(test_the_event_task_gives_queued_listeners_a_copy);        // starts the tasks: they stay
  RUN_TEST(test_a_queued_listener_hears_only_its_lane_in_its_lanes_task);
  RUN_TEST(test_a_deferred_publish_runs_the_inline_listeners_in_the_event_task);
  RUN_TEST(test_an_event_that_does_not_fit_the_hold_goes_to_the_event_task);
  RUN_TEST(test_a_publish_from_an_isr_is_deferred_and_a_drop_is_reported_by_the_task);
  RUN_TEST(test_a_hold_of_one_task_does_not_hold_another);
  return UNITY_END();
}
