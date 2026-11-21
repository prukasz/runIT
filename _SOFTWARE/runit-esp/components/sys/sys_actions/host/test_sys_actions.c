/* Host tests of sys_actions: a record is written the way a client does it (BEGIN, WRITE, COMMIT through the router),
 * read back byte for byte, kept across a restart of the module, and replayed: by a RUN command, by RUN_ALL, nested,
 * and injected by the board itself (sys_actions_request). The router's two tasks are real threads; the frames of a
 * record land in a recording module (REC), so what a replay does is what REC saw. NVS is a table in RAM (host_nvs.c).
 * Run: python test_host/run.py sys_actions   (runit-esp-archive/test_host/HOST_TESTS.MD) */
#include <pthread.h>
#include <string.h>
#include <time.h>

#include "esp_rom_crc.h"
#include "host_assert.h"
#include "host_mocks.h"
#include "module_id.h"
#include "nvs.h"
#include "router_queue.h"
#include "sys_actions.h"
#include "sys_actions_router.h"
#include "sys_device.h"
#include "unity.h"

#define WAIT_MS 3000
#define ACTIONS MODULE_ID_ACTIONS

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

/* ---- REC: the module the frames of a record land in ----------------------------------------------------------- */

MODULE_ID_DECLARE(MODULE_ID_REC, 0xE7)
MODULE_ID_DECLARE(MODULE_ID_PROBE, 0xE8)

#define REC_FAIL 0xEE  /* a frame whose first byte is this fails */
#define REC_MAX  16
#define REC_BYTES 12

typedef struct {
  uint8_t dir;
  uint8_t len;
  uint8_t data[REC_BYTES];
} rec_t;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static rec_t s_rec[REC_MAX];
static int s_recn;
static latch_t s_rec_in = LATCH_INIT;

static err_h h_rec(CMD_F_PARAMS) {  /* in: [key][dir][payload] */
  (void)ctx;
  (void)out;
  pthread_mutex_lock(&s_lock);
  rec_t* r = &s_rec[s_recn++ % REC_MAX];
  r->dir = in.data[1];
  r->len = (uint8_t)(in.len - 2 < REC_BYTES ? in.len - 2 : REC_BYTES);
  memcpy(r->data, in.data + 2, r->len);
  pthread_mutex_unlock(&s_lock);
  latch_post(&s_rec_in);
  return (in.len > 2 && in.data[2] == REC_FAIL) ? ERR_NEW(ERR_BAD_ARG) : NULL;
}
ROUTER_MODULE_REGISTER(MODULE_ID_REC, h_rec)

/* PROBE: runs sys_actions_run on a router task and keeps the chain it answered. in: [key][dir][u32 id][keep] */
typedef struct {
  int depth;
  uint32_t tags[32];
  uint16_t step;  /* of the outermost ERR_ACTION_STEP */
  uint32_t id;
  bool is_null;
} probe_t;
static probe_t s_probe;
static latch_t s_probe_done = LATCH_INIT;

static err_h h_probe(CMD_F_PARAMS) {
  (void)ctx;
  (void)out;
  uint32_t id;
  memcpy(&id, in.data + 2, sizeof(id));
  err_h e = sys_actions_run(id, in.data[6] != 0);
  pthread_mutex_lock(&s_lock);
  s_probe = (probe_t){.is_null = e == NULL};
  for (err_h n = e; n != NULL; n = err_cause(n)) {
    if (s_probe.depth < 32) {
      s_probe.tags[s_probe.depth] = err_tag(n);
    }
    s_probe.depth++;
  }
  if (e != NULL && err_tag(e) == ERR_ACTION_STEP) {
    s_probe.step = ERR_DATA(e, ERR_ACTION_STEP)->step;
    s_probe.id = ERR_DATA(e, ERR_ACTION_STEP)->id;
  }
  pthread_mutex_unlock(&s_lock);
  err_free(e);
  latch_post(&s_probe_done);
  return NULL;
}
ROUTER_MODULE_REGISTER(MODULE_ID_PROBE, h_probe)

/* ---- answers -------------------------------------------------------------------------------------------------- */

typedef struct {
  uint8_t status;
  uint32_t len;
  uint8_t data[CMD_FRAME_MAX];
} answer_t;
static answer_t s_ans;
static latch_t s_ans_in = LATCH_INIT;

static err_h done_cb(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
  (void)ctx;
  (void)cmd;
  pthread_mutex_lock(&s_lock);
  s_ans.status = status;
  s_ans.len = len;
  memcpy(s_ans.data, data, len);
  pthread_mutex_unlock(&s_lock);
  latch_post(&s_ans_in);
  return NULL;
}

/* A client's command, through the router: waits for its answer. */
static void call(const uint8_t* req, uint32_t len) {
  static uint16_t seq;
  const cmd_req_t cmd = {.req = req, .req_len = len, .seq = ++seq, .src = 0x07, .flags = 0};
  TEST_ASSERT_NULL(router_submit(&cmd, done_cb, NULL));
  WAIT(s_ans_in);
}

#define CALL(...)                          \
  do {                                     \
    const uint8_t _req[] = {__VA_ARGS__};  \
    call(_req, sizeof(_req));              \
  } while (0)

#define U32(v) (uint8_t)(v), (uint8_t)((v) >> 8), (uint8_t)((v) >> 16), (uint8_t)((v) >> 24)
#define U16(v) (uint8_t)(v), (uint8_t)((v) >> 8)

#define W CMD_DIR_WRITE
#define R CMD_DIR_READ

/* ---- images --------------------------------------------------------------------------------------------------- */

typedef struct {
  uint8_t b[256];
  uint16_t n;
} image_t;

/* Adds a frame: [u16 len][bytes]. */
static void add(image_t* im, const uint8_t* frame, uint16_t len) {
  im->b[im->n++] = (uint8_t)len;
  im->b[im->n++] = (uint8_t)(len >> 8);
  memcpy(im->b + im->n, frame, len);
  im->n += len;
}
#define ADD(im, ...)                          \
  do {                                        \
    const uint8_t _f[] = {__VA_ARGS__};       \
    add((im), _f, sizeof(_f));                \
  } while (0)

/* The frame that runs record `id`: [ACTIONS][W][RUN][id]. */
#define RUN_FRAME(id) ACTIONS, W, ACTIONS_OP_RUN, U32(id)

#define A_ID SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_USER, 10)
#define B_ID SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_USER, 11)
#define C_ID SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_USER, 12)

/* Writes a record the way the app does: BEGIN, WRITE in pieces of `piece` bytes, COMMIT with the CRC. */
static void upload(uint32_t id, const image_t* im, uint16_t piece) {
  CALL(ACTIONS, W, ACTIONS_OP_BEGIN, U32(id), U16(im->n));
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, s_ans.status);
  for (uint16_t off = 0; off < im->n; off += piece) {
    const uint16_t n = (uint16_t)(im->n - off < piece ? im->n - off : piece);
    uint8_t req[3 + 2 + 64] = {ACTIONS, W, ACTIONS_OP_WRITE, U16(off)};
    memcpy(req + 5, im->b + off, n);
    call(req, 5u + n);
    TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, s_ans.status);
  }
  const uint32_t crc = esp_rom_crc32_le(0, im->b, im->n);
  CALL(ACTIONS, W, ACTIONS_OP_COMMIT, U32(crc));
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, s_ans.status);
}

/* Reads a record back through the router, `piece` bytes a time, into `out`; returns its size. */
static uint16_t download(uint32_t id, uint8_t* out, uint16_t piece) {
  uint16_t got = 0;
  while (true) {
    CALL(ACTIONS, R, ACTIONS_OP_READ, U32(id), U16(got));
    TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, s_ans.status);
    if (s_ans.len == 0) {
      return got;
    }
    const uint16_t n = (uint16_t)(s_ans.len < piece ? s_ans.len : piece);
    memcpy(out + got, s_ans.data, n);
    got = (uint16_t)(got + n);
  }
}

static void wait_rec(int n) {
  for (int i = 0; i < n; i++) {
    WAIT(s_rec_in);
  }
}

/* ---- setup ---------------------------------------------------------------------------------------------------- */

void setUp(void) {
  host_log_clear();
  host_nvs_reset();
  TEST_ASSERT_NULL(sys_actions_init());
  pthread_mutex_lock(&s_lock);
  s_recn = 0;
  memset(&s_probe, 0, sizeof(s_probe));
  pthread_mutex_unlock(&s_lock);
  latch_clear(&s_rec_in);
  latch_clear(&s_ans_in);
  latch_clear(&s_probe_done);
}

void tearDown(void) {
  sys_actions_abort();
}

#define ASSERT_ERR(tag, e)   \
  do {                       \
    err_h _e = (e);          \
    TEST_ASSERT_ERR_TAG(tag, _e); \
    err_free(_e);            \
  } while (0)

/* ---- the stand-ins -------------------------------------------------------------------------------------------- */

static void test_the_crc_is_zlibs(void) {
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, esp_rom_crc32_le(0, (const uint8_t*)"123456789", 9));
}

/* ---- the onboard actions -------------------------------------------------------------------------------------- */

static void test_init_stores_the_four_onboard_actions(void) {
  uint32_t ids[8];
  uint16_t count = 0, total = 0;
  TEST_ASSERT_NULL(sys_actions_list(0, ids, 8, &count, &total));
  TEST_ASSERT_EQUAL_UINT16(4, total);
  TEST_ASSERT_EQUAL_UINT16(4, count);
  TEST_ASSERT_EQUAL_UINT32(SYS_ACTIONS_ONBOARD_RESUME, ids[0]);
  TEST_ASSERT_EQUAL_UINT32(SYS_ACTIONS_ONBOARD_HARD_RESET, ids[3]);

  uint8_t buf[16];
  uint32_t len = 0;
  TEST_ASSERT_NULL(sys_actions_read(SYS_ACTIONS_ONBOARD_RESUME, 0, buf, sizeof(buf), &len));
  /* the sweep frame: [len 5][device router][write][every device][RESUME u16] */
  const uint8_t want[] = {5, 0, MODULE_ID_SYS_DEVICE, W, SYS_DEVICE_ID_NONE, U16(CONTRACT_SYS_DEVICE_OP_RESUME)};
  TEST_ASSERT_EQUAL_UINT32(sizeof(want), len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, buf, sizeof(want));
}

static void test_init_again_writes_nothing_and_restores_an_edited_onboard_action(void) {
  const uint32_t writes = host_nvs_writes();
  TEST_ASSERT_NULL(sys_actions_init());
  TEST_ASSERT_EQUAL_UINT32(writes, host_nvs_writes());  /* no flash wear on every boot */

  image_t im = {0};
  ADD(&im, MODULE_ID_REC, W, 1, 2);
  TEST_ASSERT_NULL(sys_actions_set(SYS_ACTIONS_ONBOARD_RESUME, im.b, im.n));
  TEST_ASSERT_EQUAL_UINT32(writes + 1, host_nvs_writes());
  TEST_ASSERT_NULL(sys_actions_init());  /* the boot puts the firmware's own back */
  uint8_t buf[16];
  uint32_t len = 0;
  TEST_ASSERT_NULL(sys_actions_read(SYS_ACTIONS_ONBOARD_RESUME, 0, buf, sizeof(buf), &len));
  TEST_ASSERT_EQUAL_UINT32(7, len);
  TEST_ASSERT_EQUAL_UINT8(MODULE_ID_SYS_DEVICE, buf[2]);
}

/* ---- storing a record ----------------------------------------------------------------------------------------- */

static void test_a_record_written_by_a_client_reads_back_byte_for_byte(void) {
  image_t im = {0};
  ADD(&im, MODULE_ID_REC, W, 0x11, 0x22);
  ADD(&im, MODULE_ID_REC, R, 0x33);
  ADD(&im, MODULE_ID_REC, W, 0x44, 0x55, 0x66, 0x77);
  upload(A_ID, &im, 5);  /* in pieces that split the frames */

  CALL(ACTIONS, R, ACTIONS_OP_INFO, U32(A_ID));  /* INFO */
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, s_ans.status);
  sys_actions_info_t info;
  TEST_ASSERT_EQUAL_UINT32(sizeof(info), s_ans.len);
  memcpy(&info, s_ans.data, sizeof(info));
  TEST_ASSERT_EQUAL_UINT16(im.n, info.bytes);
  TEST_ASSERT_EQUAL_UINT16(3, info.count);
  TEST_ASSERT_EQUAL_HEX32(esp_rom_crc32_le(0, im.b, im.n), info.crc32);

  uint8_t back[256];
  TEST_ASSERT_EQUAL_UINT16(im.n, download(A_ID, back, 7));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(im.b, back, im.n);
}

static void test_a_record_survives_a_restart_of_the_module(void) {
  image_t im = {0};
  ADD(&im, MODULE_ID_REC, W, 0x01);
  upload(A_ID, &im, 64);
  const uint32_t writes = host_nvs_writes();
  TEST_ASSERT_NULL(sys_actions_init());  /* "a reboot": the namespace opened again, the onboard actions set again */
  TEST_ASSERT_EQUAL_UINT32(writes, host_nvs_writes());
  uint8_t back[64];
  TEST_ASSERT_EQUAL_UINT16(im.n, download(A_ID, back, 64));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(im.b, back, im.n);
  /* nothing replays by itself */
  TEST_ASSERT_EQUAL_INT(0, s_recn);
}

static void test_a_second_upload_replaces_the_record(void) {
  image_t one = {0}, two = {0};
  ADD(&one, MODULE_ID_REC, W, 0x01);
  ADD(&two, MODULE_ID_REC, W, 0x02, 0x03);
  upload(A_ID, &one, 64);
  upload(A_ID, &two, 64);
  uint8_t back[64];
  TEST_ASSERT_EQUAL_UINT16(two.n, download(A_ID, back, 64));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(two.b, back, two.n);
}

static void test_reading_past_the_image_is_empty_at_the_end_and_an_error_beyond(void) {
  image_t im = {0};
  ADD(&im, MODULE_ID_REC, W, 0x01);
  TEST_ASSERT_NULL(sys_actions_set(A_ID, im.b, im.n));
  uint8_t buf[8];
  uint32_t len = 99;
  TEST_ASSERT_NULL(sys_actions_read(A_ID, im.n, buf, sizeof(buf), &len));
  TEST_ASSERT_EQUAL_UINT32(0, len);
  ASSERT_ERR(ERR_BAD_OFFSET, sys_actions_read(A_ID, (uint16_t)(im.n + 1), buf, sizeof(buf), &len));
  ASSERT_ERR(ERR_ACTION_NOT_FOUND, sys_actions_read(B_ID, 0, buf, sizeof(buf), &len));
}

static void test_a_bad_upload_stores_nothing_and_closes_the_draft(void) {
  image_t im = {0};
  ADD(&im, MODULE_ID_REC, W, 0x01);
  /* a wrong CRC */
  TEST_ASSERT_NULL(sys_actions_record_begin(A_ID, im.n));
  TEST_ASSERT_NULL(sys_actions_record_write(0, im.b, im.n));
  err_h e = sys_actions_record_commit(0x12345678u);
  TEST_ASSERT_ERR_TAG(ERR_ACTION_CRC, e);
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, ERR_DATA(e, ERR_ACTION_CRC)->got);
  err_free(e);
  sys_actions_info_t info;
  ASSERT_ERR(ERR_ACTION_NOT_FOUND, sys_actions_info(A_ID, &info));
  ASSERT_ERR(ERR_ACTION_NO_DRAFT, sys_actions_record_commit(0));  /* the failed commit closed the draft */

  /* a frame that runs past the end of the image */
  uint8_t broken[] = {9, 0, MODULE_ID_REC, W};
  TEST_ASSERT_NULL(sys_actions_record_begin(A_ID, sizeof(broken)));
  TEST_ASSERT_NULL(sys_actions_record_write(0, broken, sizeof(broken)));
  ASSERT_ERR(ERR_ACTION_BAD_FRAME, sys_actions_record_commit(esp_rom_crc32_le(0, broken, sizeof(broken))));
  ASSERT_ERR(ERR_ACTION_NOT_FOUND, sys_actions_info(A_ID, &info));

  /* not every byte came */
  TEST_ASSERT_NULL(sys_actions_record_begin(A_ID, im.n));
  TEST_ASSERT_NULL(sys_actions_record_write(0, im.b, 2));
  ASSERT_ERR(ERR_ACTION_INCOMPLETE, sys_actions_record_commit(0));
}

static void test_the_draft_is_one_at_a_time_and_in_order(void) {
  const uint8_t bytes[8] = {0};
  ASSERT_ERR(ERR_ACTION_NO_DRAFT, sys_actions_record_write(0, bytes, 1));
  TEST_ASSERT_NULL(sys_actions_record_begin(A_ID, 8));
  ASSERT_ERR(ERR_BUSY, sys_actions_record_begin(B_ID, 8));
  ASSERT_ERR(ERR_BAD_OFFSET, sys_actions_record_write(3, bytes, 1));  /* out of order */
  ASSERT_ERR(ERR_ACTION_TOO_BIG, sys_actions_record_write(0, bytes, 9)); /* more than announced */
  sys_actions_abort();
  TEST_ASSERT_NULL(sys_actions_record_begin(B_ID, 8));  /* abort frees it */
}

static void test_the_limits_of_a_record(void) {
  ASSERT_ERR(ERR_ACTION_BAD_ID, sys_actions_record_begin(SYS_ACTIONS_ID_NONE, 4));
  ASSERT_ERR(ERR_ACTION_EMPTY, sys_actions_record_begin(A_ID, 0));
  ASSERT_ERR(ERR_ACTION_TOO_BIG, sys_actions_record_begin(A_ID, CONFIG_SYS_ACTIONS_MAX_BYTES + 1));
  uint8_t one[] = {1, 0, MODULE_ID_REC};
  ASSERT_ERR(ERR_ACTION_BAD_ID, sys_actions_set(SYS_ACTIONS_ID_NONE, one, sizeof(one)));
  uint8_t zero_len[] = {0, 0};
  ASSERT_ERR(ERR_ACTION_BAD_FRAME, sys_actions_set(A_ID, zero_len, sizeof(zero_len)));  /* an empty frame */
  TEST_ASSERT_NULL(sys_actions_set(A_ID, one, sizeof(one)));
}

/* ---- the list, copy, dump, remove ----------------------------------------------------------------------------- */

static void test_list_is_ascending_and_pages_by_the_last_id(void) {
  image_t im = {0};
  ADD(&im, MODULE_ID_REC, W, 0x01);
  const uint32_t ids[] = {C_ID, A_ID, B_ID};
  for (unsigned i = 0; i < 3; i++) {
    TEST_ASSERT_NULL(sys_actions_set(ids[i], im.b, im.n));
  }
  uint32_t got[2];
  uint16_t count = 0, total = 0;
  TEST_ASSERT_NULL(sys_actions_list(A_ID, got, 2, &count, &total));
  TEST_ASSERT_EQUAL_UINT16(7, total);  /* 4 onboard + 3 */
  TEST_ASSERT_EQUAL_UINT16(2, count);
  TEST_ASSERT_EQUAL_UINT32(A_ID, got[0]);
  TEST_ASSERT_EQUAL_UINT32(B_ID, got[1]);
  TEST_ASSERT_NULL(sys_actions_list(B_ID + 1, got, 2, &count, &total));
  TEST_ASSERT_EQUAL_UINT16(1, count);
  TEST_ASSERT_EQUAL_UINT32(C_ID, got[0]);

  CALL(ACTIONS, R, ACTIONS_OP_LIST, U32(A_ID));  /* LIST through the router: u16 total, then the IDs */
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, s_ans.status);
  TEST_ASSERT_EQUAL_UINT32(2u + 3u * 4u, s_ans.len);
  uint16_t t;
  memcpy(&t, s_ans.data, sizeof(t));
  TEST_ASSERT_EQUAL_UINT16(7, t);
  uint32_t first;
  memcpy(&first, s_ans.data + 2, sizeof(first));
  TEST_ASSERT_EQUAL_UINT32(A_ID, first);
}

static err_h on_frame(const uint8_t* frame, uint16_t len, bool last, void* ctx) {
  (void)ctx;
  pthread_mutex_lock(&s_lock);
  rec_t* r = &s_rec[s_recn++ % REC_MAX];
  r->dir = last ? 1 : 0;
  r->len = (uint8_t)(len < REC_BYTES ? len : REC_BYTES);
  memcpy(r->data, frame, r->len);
  pthread_mutex_unlock(&s_lock);
  return NULL;
}

static void test_dump_walks_the_frames_as_recorded_and_copy_makes_the_same_record(void) {
  image_t im = {0};
  ADD(&im, MODULE_ID_REC, W, 0x01);
  ADD(&im, MODULE_ID_REC, R, 0x02, 0x03);
  TEST_ASSERT_NULL(sys_actions_set(A_ID, im.b, im.n));
  TEST_ASSERT_NULL(sys_actions_copy(A_ID, B_ID));
  TEST_ASSERT_NULL(sys_actions_dump(B_ID, on_frame, NULL));
  TEST_ASSERT_EQUAL_INT(2, s_recn);
  TEST_ASSERT_EQUAL_UINT8(0, s_rec[0].dir);  /* not the last */
  TEST_ASSERT_EQUAL_UINT8(1, s_rec[1].dir);  /* the last */
  TEST_ASSERT_EQUAL_UINT8(4, s_rec[1].len);
  const uint8_t second[] = {MODULE_ID_REC, R, 0x02, 0x03};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(second, s_rec[1].data, sizeof(second));
  const uint32_t writes = host_nvs_writes();
  TEST_ASSERT_NULL(sys_actions_copy(A_ID, B_ID));  /* the same image: nothing written */
  TEST_ASSERT_EQUAL_UINT32(writes, host_nvs_writes());
  ASSERT_ERR(ERR_ACTION_NOT_FOUND, sys_actions_copy(C_ID, A_ID));
}

static void test_remove_and_remove_all(void) {
  image_t im = {0};
  ADD(&im, MODULE_ID_REC, W, 0x01);
  TEST_ASSERT_NULL(sys_actions_set(A_ID, im.b, im.n));
  CALL(ACTIONS, W, ACTIONS_OP_REMOVE, U32(A_ID));  /* REMOVE */
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, s_ans.status);
  ASSERT_ERR(ERR_ACTION_NOT_FOUND, sys_actions_remove(A_ID));
  TEST_ASSERT_NULL(sys_actions_set(A_ID, im.b, im.n));
  TEST_ASSERT_NULL(sys_actions_record_begin(B_ID, 4));
  ASSERT_ERR(ERR_BUSY, sys_actions_remove_all());  /* while a record is being written */
  sys_actions_abort();
  CALL(ACTIONS, W, ACTIONS_OP_REMOVE_ALL);  /* REMOVE_ALL */
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, s_ans.status);
  TEST_ASSERT_EQUAL_UINT32(0, host_nvs_count());
  TEST_ASSERT_NULL(sys_actions_init());  /* the onboard actions come back with the next boot */
  TEST_ASSERT_EQUAL_UINT32(4, host_nvs_count());
}

/* ---- replaying: injecting what was stored --------------------------------------------------------------------- */

static void test_run_injects_every_frame_in_order_with_its_own_direction(void) {
  image_t im = {0};
  ADD(&im, MODULE_ID_REC, W, 0x11, 0x22);
  ADD(&im, MODULE_ID_REC, R, 0x33);
  ADD(&im, MODULE_ID_REC, W, 0x44);
  upload(A_ID, &im, 64);

  CALL(ACTIONS, W, ACTIONS_OP_RUN, U32(A_ID));  /* RUN */
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, s_ans.status);
  TEST_ASSERT_EQUAL_INT(3, s_recn);
  TEST_ASSERT_EQUAL_UINT8(W, s_rec[0].dir);
  TEST_ASSERT_EQUAL_UINT8(2, s_rec[0].len);
  TEST_ASSERT_EQUAL_UINT8(0x11, s_rec[0].data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x22, s_rec[0].data[1]);
  TEST_ASSERT_EQUAL_UINT8(R, s_rec[1].dir);
  TEST_ASSERT_EQUAL_UINT8(0x33, s_rec[1].data[0]);
  TEST_ASSERT_EQUAL_UINT8(W, s_rec[2].dir);
  TEST_ASSERT_EQUAL_UINT8(0x44, s_rec[2].data[0]);
  TEST_ASSERT_EQUAL_UINT32(0, s_ans.len);  /* the answers of the frames are dropped */
}

static void test_a_record_can_be_run_again_and_is_not_consumed(void) {
  image_t im = {0};
  ADD(&im, MODULE_ID_REC, W, 0x01);
  TEST_ASSERT_NULL(sys_actions_set(A_ID, im.b, im.n));
  for (int i = 0; i < 3; i++) {
    CALL(ACTIONS, W, ACTIONS_OP_RUN, U32(A_ID));
    TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, s_ans.status);
  }
  TEST_ASSERT_EQUAL_INT(3, s_recn);
}

static void test_the_board_injects_a_record_with_request_normal_and_urgent(void) {
  image_t im = {0};
  ADD(&im, MODULE_ID_REC, W, 0x01);
  ADD(&im, MODULE_ID_REC, W, 0x02);
  TEST_ASSERT_NULL(sys_actions_set(A_ID, im.b, im.n));
  TEST_ASSERT_NULL(sys_actions_request(A_ID, false));
  wait_rec(2);
  TEST_ASSERT_EQUAL_UINT8(0x01, s_rec[0].data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x02, s_rec[1].data[0]);
  TEST_ASSERT_NULL(sys_actions_request(A_ID, true));
  wait_rec(2);
  TEST_ASSERT_EQUAL_INT(4, s_recn);
  ASSERT_ERR(ERR_ACTION_BAD_ID, sys_actions_request(SYS_ACTIONS_ID_NONE, false));
}

static void test_run_stops_at_the_first_failing_frame_and_names_it(void) {
  image_t im = {0};
  ADD(&im, MODULE_ID_REC, W, 0x01);
  ADD(&im, MODULE_ID_REC, W, REC_FAIL);
  ADD(&im, MODULE_ID_REC, W, 0x03);
  TEST_ASSERT_NULL(sys_actions_set(A_ID, im.b, im.n));
  CALL(MODULE_ID_PROBE, W, U32(A_ID), 0);  /* keep_going false */
  WAIT(s_probe_done);
  TEST_ASSERT_FALSE(s_probe.is_null);
  TEST_ASSERT_EQUAL_UINT32(ERR_ACTION_STEP, s_probe.tags[0]);
  TEST_ASSERT_EQUAL_UINT16(1, s_probe.step);  /* counted from 0 */
  TEST_ASSERT_EQUAL_UINT32(A_ID, s_probe.id);
  TEST_ASSERT_EQUAL_UINT32(ERR_BAD_ARG, s_probe.tags[s_probe.depth - 1]);  /* the cause is under it */
  TEST_ASSERT_EQUAL_INT(2, s_recn);  /* the third frame did not run */

  /* the same record through a remote RUN answers ERROR */
  CALL(ACTIONS, W, ACTIONS_OP_RUN, U32(A_ID));
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_ERROR, s_ans.status);
}

static void test_run_all_runs_every_frame_and_returns_the_first_failure(void) {
  image_t im = {0};
  ADD(&im, MODULE_ID_REC, W, REC_FAIL);
  ADD(&im, MODULE_ID_REC, W, 0x02);
  ADD(&im, MODULE_ID_REC, W, REC_FAIL);
  TEST_ASSERT_NULL(sys_actions_set(A_ID, im.b, im.n));
  CALL(MODULE_ID_PROBE, W, U32(A_ID), 1);  /* keep_going */
  WAIT(s_probe_done);
  TEST_ASSERT_FALSE(s_probe.is_null);
  TEST_ASSERT_EQUAL_UINT16(0, s_probe.step);  /* the first failure is the answer */
  TEST_ASSERT_EQUAL_INT(3, s_recn);           /* all three ran */
  CALL(ACTIONS, W, ACTIONS_OP_RUN_ALL, U32(A_ID));          /* RUN_ALL through the router */
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_ERROR, s_ans.status);
  TEST_ASSERT_EQUAL_INT(6, s_recn);
}

static void test_a_run_frame_inside_a_record_runs_the_other_record(void) {
  image_t inner = {0}, outer = {0};
  ADD(&inner, MODULE_ID_REC, W, 0x0A);
  ADD(&inner, MODULE_ID_REC, W, 0x0B);
  TEST_ASSERT_NULL(sys_actions_set(A_ID, inner.b, inner.n));
  ADD(&outer, MODULE_ID_REC, W, 0x01);
  ADD(&outer, RUN_FRAME(A_ID));
  ADD(&outer, MODULE_ID_REC, W, 0x02);
  TEST_ASSERT_NULL(sys_actions_set(B_ID, outer.b, outer.n));
  CALL(ACTIONS, W, ACTIONS_OP_RUN, U32(B_ID));
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, s_ans.status);
  TEST_ASSERT_EQUAL_INT(4, s_recn);
  const uint8_t order[] = {0x01, 0x0A, 0x0B, 0x02};
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_EQUAL_UINT8(order[i], s_rec[i].data[0]);
  }
}

static void test_a_record_that_runs_itself_stops_at_the_depth_limit(void) {
  image_t im = {0};
  ADD(&im, MODULE_ID_REC, W, 0x01);
  ADD(&im, RUN_FRAME(A_ID));
  TEST_ASSERT_NULL(sys_actions_set(A_ID, im.b, im.n));
  CALL(MODULE_ID_PROBE, W, U32(A_ID), 0);
  WAIT(s_probe_done);
  TEST_ASSERT_FALSE(s_probe.is_null);
  /* the REC frame ran once per level: the first level is the run itself, then MAX_DEPTH - 1 nested ones... */
  TEST_ASSERT_EQUAL_INT(CONFIG_SYS_ACTIONS_MAX_DEPTH, s_recn);
  bool depth_seen = false;
  for (int i = 0; i < s_probe.depth && i < 32; i++) {
    depth_seen = depth_seen || s_probe.tags[i] == ERR_ACTION_DEPTH;
  }
  TEST_ASSERT_TRUE_MESSAGE(depth_seen, "ERR_ACTION_DEPTH not in the chain");
  TEST_ASSERT_TRUE(s_probe.depth > 3);
}

static void test_a_missing_record_and_a_run_off_the_router_tasks_are_refused(void) {
  ASSERT_ERR(ERR_ACTION_WRONG_TASK, sys_actions_run(A_ID, false));  /* the test thread is no router task */
  CALL(ACTIONS, W, ACTIONS_OP_RUN, U32(A_ID));
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_ERROR, s_ans.status);
  CALL(ACTIONS, R, ACTIONS_OP_INFO, U32(A_ID));  /* INFO of nothing */
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_ERROR, s_ans.status);
  CALL(ACTIONS, W, ACTIONS_OP_RUN, U32(SYS_ACTIONS_ID_NONE));
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_ERROR, s_ans.status);
}

int main(void) {
  TEST_ASSERT_NULL(router_init());

  UNITY_BEGIN();
  RUN_TEST(test_the_crc_is_zlibs);
  RUN_TEST(test_init_stores_the_four_onboard_actions);
  RUN_TEST(test_init_again_writes_nothing_and_restores_an_edited_onboard_action);
  RUN_TEST(test_a_record_written_by_a_client_reads_back_byte_for_byte);
  RUN_TEST(test_a_record_survives_a_restart_of_the_module);
  RUN_TEST(test_a_second_upload_replaces_the_record);
  RUN_TEST(test_reading_past_the_image_is_empty_at_the_end_and_an_error_beyond);
  RUN_TEST(test_a_bad_upload_stores_nothing_and_closes_the_draft);
  RUN_TEST(test_the_draft_is_one_at_a_time_and_in_order);
  RUN_TEST(test_the_limits_of_a_record);
  RUN_TEST(test_list_is_ascending_and_pages_by_the_last_id);
  RUN_TEST(test_dump_walks_the_frames_as_recorded_and_copy_makes_the_same_record);
  RUN_TEST(test_remove_and_remove_all);
  RUN_TEST(test_run_injects_every_frame_in_order_with_its_own_direction);
  RUN_TEST(test_a_record_can_be_run_again_and_is_not_consumed);
  RUN_TEST(test_the_board_injects_a_record_with_request_normal_and_urgent);
  RUN_TEST(test_run_stops_at_the_first_failing_frame_and_names_it);
  RUN_TEST(test_run_all_runs_every_frame_and_returns_the_first_failure);
  RUN_TEST(test_a_run_frame_inside_a_record_runs_the_other_record);
  RUN_TEST(test_a_record_that_runs_itself_stops_at_the_depth_limit);
  RUN_TEST(test_a_missing_record_and_a_run_off_the_router_tasks_are_refused);
  return UNITY_END();
}
