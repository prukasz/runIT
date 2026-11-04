/* Host tests of the router's two tasks, run for real: the work task and the urgent task are threads (test_host's
 * FreeRTOS stand-in), the queues are the router's own. A command goes in with router_submit / router_inject and its
 * answer comes back to the done callback; the tests wait on latches, never on guesses, except where "nothing happened
 * yet" is the point (a short settle, to show a command did NOT run).
 * Run: python test_host/run.py router   (runit-esp/test_host/HOST_TESTS.MD) */
#include <pthread.h>
#include <string.h>
#include <time.h>
#include "unity.h"
#include "host_assert.h"
#include "router.h"
#include "host_mocks.h"

#define WAIT_MS 3000

/* ---- a latch: post from a task, wait from the test ------------------------------------------------------------- */

typedef struct {
  pthread_mutex_t m;
  pthread_cond_t c;
  int n;
} latch_t;
#define LATCH_INIT {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0}

static void latch_post(latch_t* l) {
  pthread_mutex_lock(&l->m);
  l->n++;
  pthread_cond_broadcast(&l->c);
  pthread_mutex_unlock(&l->m);
}

/* Takes one post; false after `ms` without one. */
static bool latch_wait(latch_t* l, int ms) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += ms / 1000;
  ts.tv_nsec += (long)(ms % 1000) * 1000000L;
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000L;
  }
  bool ok = true;
  pthread_mutex_lock(&l->m);
  while (l->n == 0 && ok) {
    ok = pthread_cond_timedwait(&l->c, &l->m, &ts) == 0;
  }
  if (l->n > 0) {
    l->n--;
    ok = true;
  }
  pthread_mutex_unlock(&l->m);
  return ok;
}

static void latch_clear(latch_t* l) {
  pthread_mutex_lock(&l->m);
  l->n = 0;
  pthread_mutex_unlock(&l->m);
}

#define WAIT(l) TEST_ASSERT_TRUE_MESSAGE(latch_wait(&(l), WAIT_MS), "timed out waiting for " #l)

/* ---- what the tasks did, recorded by the handlers and callbacks ----------------------------------------------- */

#define MAX_REC 16

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {  /* a handler ran */
  uint8_t key;
  bool has_origin;
  uint8_t src;
  uint16_t seq;
  uint8_t flags;
  uint32_t req_len;
  uint8_t req0;
  bool on_task;
  pthread_t thread;
} hrec_t;
static hrec_t s_h[MAX_REC];
static int s_hn;

typedef struct {  /* an answer reached its done */
  uint8_t status;
  uint32_t len;
  uint8_t data[CMD_FRAME_MAX];
  uint8_t src;
  uint16_t seq;
  uint8_t flags;
  void* ctx;
  bool on_task;
} drec_t;
static drec_t s_d[MAX_REC];
static int s_dn;
static latch_t s_done = LATCH_INIT;
static bool s_done_fails;

typedef struct {  /* a chain reached the error sink */
  uint32_t tags[4];  /* outermost first */
  int depth;
  bool has_cmd;
  uint8_t src;
  uint16_t seq;
  uint8_t flags;
  uint32_t req_len;
  uint8_t req0;
} srec_t;
static srec_t s_s[MAX_REC];
static int s_sn;
static latch_t s_sink = LATCH_INIT;

typedef struct {  /* the tap saw a command */
  uint8_t src;
  uint32_t result_tag;  /* 0: success */
} trec_t;
static trec_t s_t[MAX_REC];
static int s_tn;
static int s_taken;

static latch_t s_entered = LATCH_INIT;  /* a blocking handler started */
static latch_t s_gate = LATCH_INIT;     /* post: lets one blocking handler go */

static void record_handler(uint8_t key, const cmd_in_t* in) {
  const cmd_req_t* o = router_origin();
  pthread_mutex_lock(&s_lock);
  hrec_t* r = &s_h[s_hn++ % MAX_REC];
  r->key = key;
  r->has_origin = o != NULL;
  r->src = o ? o->src : 0;
  r->seq = o ? o->seq : 0;
  r->flags = o ? o->flags : 0;
  r->req_len = o ? o->req_len : 0;
  r->req0 = o && o->req_len > 0 ? o->req[0] : 0;
  r->on_task = router_is_task();
  r->thread = pthread_self();
  pthread_mutex_unlock(&s_lock);
  (void)in;
}

/* ---- the root handlers ---------------------------------------------------------------------------------------- */

#define KEY_ECHO  0x31
#define KEY_FAIL  0x32
#define KEY_BLOCK 0x33
#define KEY_BIG   0x34
#define KEY_NEST  0x35

static err_h h_echo(CMD_F_PARAMS) {  /* answers what follows the key: [dir][payload] */
  (void)ctx;
  record_handler(KEY_ECHO, &in);
  return cmd_reply(out, in.data + 1, in.len - 1);
}

static err_h h_fail(CMD_F_PARAMS) {  /* replies, then fails: the answer must be the error, not this */
  (void)ctx;
  record_handler(KEY_FAIL, &in);
  const uint8_t half[] = {1, 2, 3};
  ERR_TRY(cmd_reply(out, half, sizeof(half)));
  return ERR_NEW(ERR_CMD_BAD_ARG);
}

static err_h h_block(CMD_F_PARAMS) {  /* holds the task that runs it until s_gate is posted */
  (void)ctx;
  record_handler(KEY_BLOCK, &in);
  latch_post(&s_entered);
  if (!latch_wait(&s_gate, 5000)) {
    return ERR_NEW(ERR_CMD_BAD_ARG);
  }
  return cmd_reply(out, "b", 1);
}

static err_h h_big(CMD_F_PARAMS) {  /* [dir][n hi][n lo]: an answer of n bytes */
  (void)ctx;
  static uint8_t big[CMD_FRAME_MAX];
  record_handler(KEY_BIG, &in);
  const uint32_t n = ((uint32_t)in.data[2] << 8) | in.data[3];
  memset(big, 0xAB, sizeof(big));
  return cmd_reply(out, big, n);
}

static err_h h_nest(CMD_F_PARAMS) {  /* runs another command in its own task, as a replay does */
  (void)ctx;
  record_handler(KEY_NEST, &in);
  uint8_t buf[16];
  cmd_out_t inner = {.data = buf, .cap = sizeof(buf), .len = 0};
  const uint8_t req[] = {KEY_ECHO, CMD_DIR_READ, 0x05};
  ERR_TRY(router_route((cmd_in_t){req, sizeof(req)}, &inner));
  return cmd_reply(out, buf, inner.len);
}

/* ---- the callbacks -------------------------------------------------------------------------------------------- */

static err_h done_cb(void* ctx, const cmd_req_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  pthread_mutex_lock(&s_lock);
  drec_t* d = &s_d[s_dn++ % MAX_REC];
  d->status = status;
  d->len = len;
  memcpy(d->data, data, len < sizeof(d->data) ? len : sizeof(d->data));
  d->src = cmd->src;
  d->seq = cmd->seq;
  d->flags = cmd->flags;
  d->ctx = ctx;
  d->on_task = router_is_task();
  pthread_mutex_unlock(&s_lock);
  latch_post(&s_done);
  return s_done_fails ? ERR_NEW(ERR_CMD_BAD_ARG) : NULL;
}

static void sink_cb(err_h chain, const struct cmd_req* cmd) {
  pthread_mutex_lock(&s_lock);
  srec_t* r = &s_s[s_sn++ % MAX_REC];
  r->depth = 0;
  for (err_h e = chain; e != NULL; e = e->next) {
    if (r->depth < 4) r->tags[r->depth] = e->tag;
    r->depth++;
  }
  r->has_cmd = cmd != NULL;
  r->src = cmd ? cmd->src : 0;
  r->seq = cmd ? cmd->seq : 0;
  r->flags = cmd ? cmd->flags : 0;
  r->req_len = cmd ? cmd->req_len : 0;
  r->req0 = cmd && cmd->req_len > 0 ? cmd->req[0] : 0;
  pthread_mutex_unlock(&s_lock);
  latch_post(&s_sink);
}

static bool tap_take(const cmd_req_t* cmd) {
  const bool take = cmd->req_len > 2 && cmd->req[2] == 0xEE;
  if (take) {
    pthread_mutex_lock(&s_lock);
    s_taken++;
    pthread_mutex_unlock(&s_lock);
  }
  return take;
}
static void tap_seen(const cmd_req_t* cmd, err_h result) {
  pthread_mutex_lock(&s_lock);
  trec_t* r = &s_t[s_tn++ % MAX_REC];
  r->src = cmd->src;
  r->result_tag = result != NULL ? result->tag : 0;
  pthread_mutex_unlock(&s_lock);
}
static const router_tap_t k_tap = {.take = tap_take, .seen = tap_seen};

static struct {  /* router_run_urgent's functions */
  latch_t ran;
  int count;
  bool on_task;
  pthread_t thread;
} s_fa = {LATCH_INIT, 0, false, 0}, s_fb = {LATCH_INIT, 0, false, 0};
static void run_fa(void) {
  s_fa.count++;
  s_fa.on_task = router_is_task();
  s_fa.thread = pthread_self();
  latch_post(&s_fa.ran);
}
static void run_fb(void) {
  s_fb.count++;
  s_fb.on_task = router_is_task();
  s_fb.thread = pthread_self();
  latch_post(&s_fb.ran);
}

/* ---- helpers -------------------------------------------------------------------------------------------------- */

void setUp(void) {
  host_log_clear();
  pthread_mutex_lock(&s_lock);
  s_hn = s_dn = s_sn = s_tn = s_taken = 0;
  pthread_mutex_unlock(&s_lock);
  s_done_fails = false;
  latch_clear(&s_done);
  latch_clear(&s_sink);
  latch_clear(&s_entered);
  latch_clear(&s_gate);
  router_set_tap(NULL);
}
void tearDown(void) {}

static err_h submit(uint8_t src, uint16_t seq, uint8_t flags, const uint8_t* req, uint32_t len, void* ctx) {
  const cmd_req_t cmd = {.req = req, .req_len = len, .seq = seq, .src = src, .flags = flags};
  return router_submit(&cmd, done_cb, ctx);
}
#define SUBMIT(src, seq, flags, ctx, ...)                                    \
  do {                                                                       \
    const uint8_t _req[] = {__VA_ARGS__};                                    \
    TEST_ASSERT_NULL(submit((src), (seq), (flags), _req, sizeof(_req), (ctx))); \
  } while (0)

/* Parses an error record in `d` into a chain (outermost first) the test checks and frees. */
static err_h chain_of(const drec_t* d) {
  err_h chain = NULL;
  err_h rc = err_record_parse(d->data, d->len, &chain);
  TEST_ASSERT_NULL(rc);
  TEST_ASSERT_NOT_NULL(chain);
  return chain;
}

/* ---- a command and its answer --------------------------------------------------------------------------------- */

static void test_a_command_runs_on_a_task_and_its_answer_goes_to_done(void) {
  int marker = 0;
  SUBMIT(0x07, 0x1234, 0, &marker, KEY_ECHO, CMD_DIR_READ, 1, 2, 3);
  WAIT(s_done);
  TEST_ASSERT_EQUAL_INT(1, s_dn);
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, s_d[0].status);
  const uint8_t want[] = {CMD_DIR_READ, 1, 2, 3};
  TEST_ASSERT_EQUAL_UINT32(sizeof(want), s_d[0].len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, s_d[0].data, sizeof(want));
  TEST_ASSERT_EQUAL_PTR(&marker, s_d[0].ctx);          /* the submitter's ctx comes back */
  TEST_ASSERT_EQUAL_UINT8(0x07, s_d[0].src);
  TEST_ASSERT_EQUAL_HEX16(0x1234, s_d[0].seq);
  TEST_ASSERT_TRUE(s_d[0].on_task);                    /* done runs on the router task */
  TEST_ASSERT_EQUAL_INT(1, s_hn);
  TEST_ASSERT_TRUE(s_h[0].on_task);
  TEST_ASSERT_TRUE(s_h[0].has_origin);                 /* the handler sees the command it is routing */
  TEST_ASSERT_EQUAL_UINT8(0x07, s_h[0].src);
  TEST_ASSERT_EQUAL_HEX16(0x1234, s_h[0].seq);
  TEST_ASSERT_EQUAL_UINT32(5, s_h[0].req_len);
  TEST_ASSERT_EQUAL_UINT8(KEY_ECHO, s_h[0].req0);
  TEST_ASSERT_FALSE(router_is_task());                 /* and the test's own thread is neither */
  TEST_ASSERT_NULL(router_origin());
}

static void test_commands_of_one_queue_run_in_order(void) {
  for (uint8_t i = 0; i < 8; i++) {
    SUBMIT(1, i, 0, NULL, KEY_ECHO, CMD_DIR_READ, i);
  }
  for (int i = 0; i < 8; i++) WAIT(s_done);
  TEST_ASSERT_EQUAL_INT(8, s_dn);
  for (int i = 0; i < 8; i++) {
    TEST_ASSERT_EQUAL_HEX16(i, s_d[i].seq);
    TEST_ASSERT_EQUAL_HEX8(i, s_d[i].data[1]);
  }
}

static void test_a_failure_answers_with_the_error_record_and_is_reported(void) {
  SUBMIT(0x09, 0x0102, 0, NULL, KEY_FAIL, CMD_DIR_READ, 0xAA);
  WAIT(s_done);
  WAIT(s_sink);
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_ERROR, s_d[0].status);
  TEST_ASSERT_EQUAL_HEX8(ERR_RECORD_SRC_COMMAND, s_d[0].data[0]);  /* not the 3 bytes the handler replied */
  const uint8_t origin[] = {0x09, 0x02, 0x01, 0x00, 3};            /* from, seq LE, flags, request length */
  TEST_ASSERT_EQUAL_HEX8_ARRAY(origin, s_d[0].data + 1 + ERR_RECORD_STAMP_BYTES, sizeof(origin));
  const uint8_t req[] = {KEY_FAIL, CMD_DIR_READ, 0xAA};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(req, s_d[0].data + 1 + ERR_RECORD_STAMP_BYTES + sizeof(origin), sizeof(req));

  err_h chain = chain_of(&s_d[0]);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_KEY, chain);
  TEST_ASSERT_EQUAL_UINT8(KEY_FAIL, ERR_DATA(chain, ERR_ROUTER_KEY)->key);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_BAD_ARG, chain->next->tag);
  err_free(chain);

  TEST_ASSERT_EQUAL_INT(1, s_sn);  /* the same chain went to the errors stream, with its command */
  TEST_ASSERT_EQUAL_HEX32(ERR_ROUTER_KEY, s_s[0].tags[0]);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_BAD_ARG, s_s[0].tags[1]);
  TEST_ASSERT_TRUE(s_s[0].has_cmd);
  TEST_ASSERT_EQUAL_UINT8(0x09, s_s[0].src);
  TEST_ASSERT_EQUAL_HEX16(0x0102, s_s[0].seq);
  TEST_ASSERT_EQUAL_UINT32(3, s_s[0].req_len);
  TEST_ASSERT_EQUAL_UINT8(KEY_FAIL, s_s[0].req0);
}

static void test_an_unknown_key_is_answered_and_reported(void) {
  SUBMIT(0x01, 5, 0, NULL, 0x6E, CMD_DIR_READ);
  WAIT(s_done);
  WAIT(s_sink);
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_ERROR, s_d[0].status);
  err_h chain = chain_of(&s_d[0]);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_UNKNOWN_KEY, chain);
  TEST_ASSERT_EQUAL_UINT8(0x6E, ERR_DATA(chain, ERR_ROUTER_UNKNOWN_KEY)->key);
  err_free(chain);
  TEST_ASSERT_EQUAL_HEX32(ERR_ROUTER_UNKNOWN_KEY, s_s[0].tags[0]);
}

static void test_noreply_has_no_answer_but_a_failure_is_still_reported(void) {
  SUBMIT(0x03, 1, ROUTER_FLAG_NOREPLY, NULL, KEY_FAIL, CMD_DIR_READ, 1);  /* fails: reported */
  SUBMIT(0x03, 2, ROUTER_FLAG_NOREPLY, NULL, KEY_ECHO, CMD_DIR_READ, 1);  /* succeeds: silent */
  SUBMIT(0x03, 3, 0, NULL, KEY_ECHO, CMD_DIR_READ, 1);                    /* behind them in the same queue */
  WAIT(s_done);
  WAIT(s_sink);
  TEST_ASSERT_EQUAL_INT(1, s_dn);                 /* only the third got an answer */
  TEST_ASSERT_EQUAL_HEX16(3, s_d[0].seq);
  TEST_ASSERT_EQUAL_INT(3, s_hn);                 /* but all three ran */
  TEST_ASSERT_EQUAL_INT(1, s_sn);
  TEST_ASSERT_EQUAL_UINT8(ROUTER_FLAG_NOREPLY, s_s[0].flags);
  TEST_ASSERT_EQUAL_HEX16(1, s_s[0].seq);
}

static void test_the_answer_may_fill_the_frame_but_not_exceed_it(void) {
  SUBMIT(1, 1, 0, NULL, KEY_BIG, CMD_DIR_READ, CMD_DATA_MAX >> 8, CMD_DATA_MAX & 0xFF);
  SUBMIT(1, 2, 0, NULL, KEY_BIG, CMD_DIR_READ, (CMD_DATA_MAX + 1) >> 8, (CMD_DATA_MAX + 1) & 0xFF);
  WAIT(s_done);
  WAIT(s_done);
  WAIT(s_sink);
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, s_d[0].status);
  TEST_ASSERT_EQUAL_UINT32(CMD_DATA_MAX, s_d[0].len);
  TEST_ASSERT_EQUAL_HEX8(0xAB, s_d[0].data[CMD_DATA_MAX - 1]);
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_ERROR, s_d[1].status);  /* one byte more: refused, not cut */
  err_h chain = chain_of(&s_d[1]);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_OUT_FULL, chain->next->tag);
  TEST_ASSERT_EQUAL_UINT16(CMD_DATA_MAX + 1, ERR_DATA(chain->next, ERR_CMD_OUT_FULL)->need);
  TEST_ASSERT_EQUAL_UINT16(CMD_DATA_MAX, ERR_DATA(chain->next, ERR_CMD_OUT_FULL)->cap);
  err_free(chain);
}

static void test_an_answer_that_could_not_be_delivered_is_reported_with_its_command(void) {
  s_done_fails = true;
  SUBMIT(0x0A, 9, 0, NULL, KEY_ECHO, CMD_DIR_READ, 1);
  WAIT(s_done);
  WAIT(s_sink);
  TEST_ASSERT_EQUAL_INT(1, s_sn);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_BAD_ARG, s_s[0].tags[0]);  /* what done returned, as it is */
  TEST_ASSERT_TRUE(s_s[0].has_cmd);
  TEST_ASSERT_EQUAL_UINT8(0x0A, s_s[0].src);
  TEST_ASSERT_EQUAL_HEX16(9, s_s[0].seq);
}

static void test_a_command_with_no_done_runs_and_a_failure_is_reported(void) {
  const uint8_t ok[] = {KEY_ECHO, CMD_DIR_READ, 1};
  const cmd_req_t c1 = {.req = ok, .req_len = sizeof(ok), .seq = 1, .src = 2, .flags = 0};
  TEST_ASSERT_NULL(router_submit(&c1, NULL, NULL));
  const uint8_t bad[] = {KEY_FAIL, CMD_DIR_READ, 1};
  const cmd_req_t c2 = {.req = bad, .req_len = sizeof(bad), .seq = 2, .src = 2, .flags = 0};
  TEST_ASSERT_NULL(router_submit(&c2, NULL, NULL));
  WAIT(s_sink);
  TEST_ASSERT_EQUAL_INT(0, s_dn);
  TEST_ASSERT_EQUAL_INT(2, s_hn);
  TEST_ASSERT_EQUAL_HEX16(2, s_s[0].seq);
}

/* ---- the two tasks run beside each other ---------------------------------------------------------------------- */

static void test_an_urgent_command_runs_while_the_work_task_is_blocked(void) {
  SUBMIT(1, 1, 0, NULL, KEY_BLOCK, CMD_DIR_READ);
  WAIT(s_entered);                                                    /* the work task is held in the handler */
  SUBMIT(1, 2, ROUTER_FLAG_URGENT, NULL, KEY_ECHO, CMD_DIR_READ, 7);
  WAIT(s_done);                                                       /* so the urgent task did this one */
  SUBMIT(1, 3, 0, NULL, KEY_ECHO, CMD_DIR_READ, 8);                   /* a normal one waits behind the blocked one */
  TEST_ASSERT_FALSE(latch_wait(&s_done, 150));

  TEST_ASSERT_EQUAL_INT(1, s_dn);
  TEST_ASSERT_EQUAL_HEX16(2, s_d[0].seq);
  TEST_ASSERT_EQUAL_INT(2, s_hn);                                     /* the blocker, then the urgent echo */
  TEST_ASSERT_EQUAL_UINT8(KEY_BLOCK, s_h[0].key);
  TEST_ASSERT_EQUAL_UINT8(KEY_ECHO, s_h[1].key);
  TEST_ASSERT_FALSE(pthread_equal(s_h[0].thread, s_h[1].thread));     /* another task */
  TEST_ASSERT_TRUE(s_h[1].on_task);
  TEST_ASSERT_EQUAL_UINT8(ROUTER_FLAG_URGENT, s_h[1].flags);

  latch_post(&s_gate);                                                /* release the work task */
  WAIT(s_done);
  WAIT(s_done);
  TEST_ASSERT_EQUAL_INT(3, s_dn);
  TEST_ASSERT_EQUAL_HEX16(1, s_d[1].seq);                             /* the blocker answers */
  TEST_ASSERT_EQUAL_HEX16(3, s_d[2].seq);                             /* then the one that waited */
}

static void test_submit_copies_the_command_and_its_request(void) {
  SUBMIT(1, 1, 0, NULL, KEY_BLOCK, CMD_DIR_READ);
  WAIT(s_entered);
  uint8_t req[] = {KEY_ECHO, CMD_DIR_READ, 0x11, 0x22, 0x33};
  cmd_req_t cmd = {.req = req, .req_len = sizeof(req), .seq = 0x00BB, .src = 0x0A, .flags = 0};
  TEST_ASSERT_NULL(router_submit(&cmd, done_cb, NULL));
  memset(req, 0xFF, sizeof(req));  /* the caller's buffers go away while the command waits in the queue */
  memset(&cmd, 0xFF, sizeof(cmd));
  latch_post(&s_gate);
  WAIT(s_done);
  WAIT(s_done);
  TEST_ASSERT_EQUAL_HEX16(0x00BB, s_d[1].seq);
  TEST_ASSERT_EQUAL_UINT8(0x0A, s_d[1].src);
  const uint8_t want[] = {CMD_DIR_READ, 0x11, 0x22, 0x33};
  TEST_ASSERT_EQUAL_UINT32(sizeof(want), s_d[1].len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, s_d[1].data, sizeof(want));
}

/* ---- the board's own commands --------------------------------------------------------------------------------- */

static void test_inject_is_the_boards_own_command_with_a_counting_seq(void) {
  const uint8_t req[] = {KEY_ECHO, CMD_DIR_READ, 4};
  TEST_ASSERT_NULL(router_inject((cmd_in_t){req, sizeof(req)}, 0, done_cb, NULL));
  TEST_ASSERT_NULL(router_inject((cmd_in_t){req, sizeof(req)}, 0, done_cb, NULL));
  WAIT(s_done);
  WAIT(s_done);
  TEST_ASSERT_EQUAL_UINT8(ROUTER_SRC_SELF, s_d[0].src);
  TEST_ASSERT_EQUAL_UINT8(ROUTER_SRC_SELF, s_d[1].src);
  TEST_ASSERT_EQUAL_HEX16((uint16_t)(s_d[0].seq + 1), s_d[1].seq);  /* the router's own counter */
  TEST_ASSERT_EQUAL_UINT8(ROUTER_SRC_SELF, s_h[0].src);
}

static void test_inject_urgent_and_noreply_flags_are_honoured(void) {
  const uint8_t req[] = {KEY_ECHO, CMD_DIR_READ, 4};
  /* The urgent queue runs in order too: a silent urgent command, then an urgent one with an answer behind it. */
  TEST_ASSERT_NULL(router_inject((cmd_in_t){req, sizeof(req)}, ROUTER_FLAG_URGENT | ROUTER_FLAG_NOREPLY, done_cb, NULL));
  TEST_ASSERT_NULL(router_inject((cmd_in_t){req, sizeof(req)}, ROUTER_FLAG_URGENT, done_cb, NULL));
  WAIT(s_done);
  TEST_ASSERT_FALSE(latch_wait(&s_done, 100));  /* the NOREPLY one is silent */
  TEST_ASSERT_EQUAL_INT(1, s_dn);
  TEST_ASSERT_EQUAL_INT(2, s_hn);               /* but it ran */
  TEST_ASSERT_EQUAL_UINT8(ROUTER_FLAG_URGENT | ROUTER_FLAG_NOREPLY, s_h[0].flags);  /* in queue order */
  TEST_ASSERT_EQUAL_UINT8(ROUTER_FLAG_URGENT, s_h[1].flags);
}

/* ---- inside a command ----------------------------------------------------------------------------------------- */

static void test_a_command_run_inside_a_handler_sees_the_outer_origin(void) {
  SUBMIT(0x0C, 0x000D, 0, NULL, KEY_NEST, CMD_DIR_READ);
  WAIT(s_done);
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, s_d[0].status);
  const uint8_t inner_answer[] = {CMD_DIR_READ, 0x05};
  TEST_ASSERT_EQUAL_UINT32(sizeof(inner_answer), s_d[0].len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(inner_answer, s_d[0].data, sizeof(inner_answer));
  TEST_ASSERT_EQUAL_INT(2, s_hn);                      /* the nest, then the echo it ran */
  TEST_ASSERT_EQUAL_UINT8(KEY_ECHO, s_h[1].key);
  TEST_ASSERT_EQUAL_UINT8(0x0C, s_h[1].src);           /* the origin is the outer command's */
  TEST_ASSERT_EQUAL_HEX16(0x000D, s_h[1].seq);
  TEST_ASSERT_EQUAL_UINT8(KEY_NEST, s_h[1].req0);
}

/* ---- the tap -------------------------------------------------------------------------------------------------- */

static void test_a_tap_can_take_a_command_and_sees_the_rest(void) {
  router_set_tap(&k_tap);
  SUBMIT(0x31, 1, 0, NULL, KEY_ECHO, CMD_DIR_READ, 0xEE);  /* the tap takes it */
  SUBMIT(0x32, 2, 0, NULL, KEY_ECHO, CMD_DIR_READ, 0x01);  /* routed, succeeds */
  SUBMIT(0x33, 3, 0, NULL, KEY_FAIL, CMD_DIR_READ, 0x01);  /* routed, fails */
  WAIT(s_done);
  WAIT(s_done);
  WAIT(s_done);
  WAIT(s_sink);
  TEST_ASSERT_EQUAL_INT(1, s_taken);
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, s_d[0].status);  /* taken: answered OK with no data */
  TEST_ASSERT_EQUAL_UINT32(0, s_d[0].len);
  TEST_ASSERT_EQUAL_INT(2, s_hn);                            /* and not routed: only the other two ran */
  TEST_ASSERT_EQUAL_INT(2, s_tn);                            /* seen() is not called for a taken command */
  TEST_ASSERT_EQUAL_UINT8(0x32, s_t[0].src);
  TEST_ASSERT_EQUAL_HEX32(0, s_t[0].result_tag);
  TEST_ASSERT_EQUAL_UINT8(0x33, s_t[1].src);
  TEST_ASSERT_EQUAL_HEX32(ERR_ROUTER_KEY, s_t[1].result_tag);  /* the failure it was shown */
  router_set_tap(NULL);
  SUBMIT(0x34, 4, 0, NULL, KEY_ECHO, CMD_DIR_READ, 0xEE);      /* without the tap it is just a command */
  WAIT(s_done);
  TEST_ASSERT_EQUAL_INT(3, s_hn);
  TEST_ASSERT_EQUAL_UINT32(2, s_d[3].len);
}

/* ---- urgent work ---------------------------------------------------------------------------------------------- */

static void test_run_urgent_runs_a_fn_once_on_the_urgent_task(void) {
  s_fa.count = s_fb.count = 0;
  latch_clear(&s_fa.ran);
  latch_clear(&s_fb.ran);
  SUBMIT(1, 1, ROUTER_FLAG_URGENT, NULL, KEY_BLOCK, CMD_DIR_READ);
  WAIT(s_entered);                                   /* the urgent task is held, so the fn can only wait */
  const pthread_t urgent_thread = s_h[0].thread;

  TEST_ASSERT_NULL(router_run_urgent(run_fa));
  TEST_ASSERT_NULL(router_run_urgent(run_fa));       /* again before it ran: still once */
  err_h e = router_run_urgent(run_fb);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_BUSY, e);  /* another fn while one waits */
  err_free(e);
  TEST_ASSERT_EQUAL_INT(0, s_fa.count);

  latch_post(&s_gate);
  WAIT(s_done);
  WAIT(s_fa.ran);
  TEST_ASSERT_FALSE(latch_wait(&s_fa.ran, 150));     /* no second run */
  TEST_ASSERT_EQUAL_INT(1, s_fa.count);
  TEST_ASSERT_TRUE(pthread_equal(urgent_thread, s_fa.thread));
  TEST_ASSERT_TRUE(s_fa.on_task);
  TEST_ASSERT_EQUAL_INT(0, s_fb.count);

  TEST_ASSERT_NULL(router_run_urgent(run_fb));       /* the slot is free again */
  WAIT(s_fb.ran);
  TEST_ASSERT_EQUAL_INT(1, s_fb.count);
}

static void test_run_urgent_wakes_an_idle_urgent_task(void) {
  s_fa.count = 0;
  latch_clear(&s_fa.ran);
  TEST_ASSERT_NULL(router_run_urgent(run_fa));
  WAIT(s_fa.ran);
  TEST_ASSERT_EQUAL_INT(1, s_fa.count);
  TEST_ASSERT_FALSE(pthread_equal(pthread_self(), s_fa.thread));  /* not on the caller's thread */
}

int main(void) {
  TEST_ASSERT_NULL(router_register(KEY_ECHO, h_echo));
  TEST_ASSERT_NULL(router_register(KEY_FAIL, h_fail));
  TEST_ASSERT_NULL(router_register(KEY_BLOCK, h_block));
  TEST_ASSERT_NULL(router_register(KEY_BIG, h_big));
  TEST_ASSERT_NULL(router_register(KEY_NEST, h_nest));
  err_set_sink(sink_cb);
  TEST_ASSERT_NULL(router_init());

  UNITY_BEGIN();
  RUN_TEST(test_a_command_runs_on_a_task_and_its_answer_goes_to_done);
  RUN_TEST(test_commands_of_one_queue_run_in_order);
  RUN_TEST(test_a_failure_answers_with_the_error_record_and_is_reported);
  RUN_TEST(test_an_unknown_key_is_answered_and_reported);
  RUN_TEST(test_noreply_has_no_answer_but_a_failure_is_still_reported);
  RUN_TEST(test_the_answer_may_fill_the_frame_but_not_exceed_it);
  RUN_TEST(test_an_answer_that_could_not_be_delivered_is_reported_with_its_command);
  RUN_TEST(test_a_command_with_no_done_runs_and_a_failure_is_reported);
  RUN_TEST(test_an_urgent_command_runs_while_the_work_task_is_blocked);
  RUN_TEST(test_submit_copies_the_command_and_its_request);
  RUN_TEST(test_inject_is_the_boards_own_command_with_a_counting_seq);
  RUN_TEST(test_inject_urgent_and_noreply_flags_are_honoured);
  RUN_TEST(test_a_command_run_inside_a_handler_sees_the_outer_origin);
  RUN_TEST(test_a_tap_can_take_a_command_and_sees_the_rest);
  RUN_TEST(test_run_urgent_runs_a_fn_once_on_the_urgent_task);
  RUN_TEST(test_run_urgent_wakes_an_idle_urgent_task);
  return UNITY_END();
}
