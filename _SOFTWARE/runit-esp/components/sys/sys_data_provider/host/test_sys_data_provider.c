/* Host tests of the provider registry (sys_data_provider.c) on fake & typed drivers: flat send, limits,
 * flags, spare rule, open/close, CREATE/REMOVE, up/down, delivery, router dispatch, INFO/LIST/OPS, and 0xFFxx own ops.
 * Run: python runit-esp-archive/test_host/run.py sys_data_provider */
#include <string.h>

#include "cmd.h"
#include "cmd_table.h"
#include "host_assert.h"
#include "host_mocks.h"
#include "sys_data_provider_driver.h"
#include "sys_data_provider_router.h"
#include "unity.h"
#include "utils.h"

/* ---- a fake UART-type driver: a queue of QUEUE_FRAMES frames, emptied only by the test ------------------------ */

#define QUEUE_FRAMES 4u

typedef struct {
  uint8_t data[CMD_FRAME_MAX];
  uint32_t len;
  uint8_t flags;
} frame_t;

typedef struct {
  sys_data_provider_t* p;
  frame_t queue[QUEUE_FRAMES];
  uint32_t queued;
  uint32_t sends;         // calls of send
  err_h fail;             // the next send returns it (an error of the driver's own)
  bool remove_in_send;    // the next send tries to remove its own provider
  err_h removed_in_send;  // what that remove returned
} fake_t;

typedef struct __attribute__((packed)) {
  uintptr_t fake;
  uint8_t state;
  bool no_send;
} fake_prov_cfg_t;

static fake_t s_fake[CONFIG_SYS_DATA_PROVIDER_MAX + 1];

static err_h fake_send(sys_data_provider_t* self, cmd_in_t frame, uint8_t flags) {
  fake_t* f = self->driver;
  f->sends++;
  if (f->remove_in_send) {
    f->remove_in_send = false;
    f->removed_in_send = sys_data_provider_write(self->id, CONTRACT_PROVIDER_OP_REMOVE, NULL, 0);
  }
  if (f->fail != NULL) {
    err_h err = f->fail;
    f->fail = NULL;
    return err;
  }
  const bool full = f->queued == QUEUE_FRAMES;
  if (full || sys_data_provider_spare_refused(flags, f->queued, 1, QUEUE_FRAMES)) {
    return ERR_NEW(ERR_PROV_BUSY, .provider = self->id);
  }
  frame_t* fr = &f->queue[f->queued++];
  memcpy(fr->data, frame.data, frame.len);
  fr->len = frame.len;
  fr->flags = flags;
  return NULL;
}

static err_h fake_create(sys_data_provider_t* p, void* ctx, const void* cfg_in) {
  (void)ctx;
  fake_prov_cfg_t cfg;
  memcpy(&cfg, cfg_in, sizeof(cfg));
  if (cfg.no_send) {
    return NULL;  // intentionally omit p->send so CREATE rejects it with ERR_BAD_ARG
  }
  fake_t* f = (fake_t*)cfg.fake;
  f->p = p;
  p->driver = f;
  p->send = fake_send;
  p->state |= cfg.state;
  return NULL;
}

static err_h fake_remove(sys_data_provider_t* p, void* ctx) {
  (void)ctx;
  fake_t* f = p->driver;
  if (f != NULL && f->p == p) {
    f->p = NULL;
  }
  return NULL;
}

SYS_DATA_PROVIDER_TYPE_DEFINE(SYS_DATA_PROVIDER_TYPE_UART, fake_prov_cfg_t, cmd_none_t, fake_create, fake_remove, NULL, 0)

static err_h fake_prov_create(uint8_t id, uint8_t type, uint8_t create_flags, const fake_prov_cfg_t* cfg) {
  uint8_t buf[SYS_DATA_PROVIDER_CREATE_HDR + sizeof(*cfg)] = {type, create_flags};
  memcpy(buf + SYS_DATA_PROVIDER_CREATE_HDR, cfg, sizeof(*cfg));
  return sys_data_provider_write(id, CONTRACT_PROVIDER_OP_CREATE, buf, sizeof(buf));
}

static err_h fake_remove_id(uint8_t id) {
  return sys_data_provider_write(id, CONTRACT_PROVIDER_OP_REMOVE, NULL, 0);
}

// Creates fake `i` as provider `id`, up, with `state` bits beside UP and `create_flags`.
static fake_t* fake_add_flags(uint32_t i, uint8_t id, uint8_t create_flags, uint8_t state) {
  fake_t* f = &s_fake[i];
  memset(f, 0, sizeof(*f));
  const fake_prov_cfg_t cfg = {
      .fake = (uintptr_t)f,
      .state = (uint8_t)(state | SYS_DATA_PROVIDER_STATE_UP),
  };
  TEST_ASSERT_NULL(fake_prov_create(id, SYS_DATA_PROVIDER_TYPE_UART, create_flags, &cfg));
  TEST_ASSERT_NOT_NULL(f->p);
  return f;
}

static fake_t* fake_add(uint32_t i, uint8_t id, uint8_t state) {
  return fake_add_flags(i, id, 0, state);
}

/* ---- a user of the rx hook ------------------------------------------------------------------------------------ */

static struct {
  uint32_t calls;
  uint8_t id;
  uint8_t data[16];
  uint32_t len;
  bool changed;  // the last call was up / down (data NULL)
} s_rx;

static void user_rx(void* ctx, uint8_t id, const uint8_t* data, uint32_t len) {
  (void)ctx;
  s_rx.calls++;
  s_rx.id = id;
  s_rx.len = len;
  s_rx.changed = data == NULL;
  if (data != NULL) {
    memcpy(s_rx.data, data, len < sizeof(s_rx.data) ? len : sizeof(s_rx.data));
  }
}

/* Every slot freed between tests: closed, the board's own flag dropped, removed. */
void setUp(void) {
  host_log_clear();
  memset(&s_rx, 0, sizeof(s_rx));
}

void tearDown(void) {
  for (uint32_t i = 0; i < ARRAY_LEN(s_fake); i++) {
    fake_t* f = &s_fake[i];
    if (f->p != NULL && f->p->driver == f) {
      const uint8_t id = f->p->id;
      err_free(sys_data_provider_close(id));
      f->p->state &= (uint8_t)~SYS_DATA_PROVIDER_STATE_SYSTEM;
      err_free(fake_remove_id(id));
    }
    memset(f, 0, sizeof(*f));
  }
}

#define ASSERT_ERR(tag, expr)       \
  do {                              \
    err_h e_ = (expr);              \
    TEST_ASSERT_ERR_TAG((tag), e_); \
    err_free(e_);                   \
  } while (0)

/* ---- flat frame send & guards --------------------------------------------------------------------------------- */

static void test_flat_frame_and_flags_reach_the_driver(void) {
  fake_t* f = fake_add(0, 7, 0);
  const uint8_t frame[] = {0xA1, 0xA2, 0xA3, 0x01, 0x02};
  TEST_ASSERT_NULL(sys_data_provider_send(7, (cmd_in_t){frame, sizeof(frame)}, CMD_SEND_URGENT));
  TEST_ASSERT_EQUAL_UINT32(1, f->queued);
  TEST_ASSERT_EQUAL_UINT32(sizeof(frame), f->queue[0].len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(frame, f->queue[0].data, sizeof(frame));
  TEST_ASSERT_EQUAL_HEX8(CMD_SEND_URGENT, f->queue[0].flags);  // the flags pass as they are
}

static void test_an_empty_frame_or_null_bytes_is_refused(void) {
  fake_t* f = fake_add(0, 7, 0);
  const uint8_t a[] = {0x11};
  ASSERT_ERR(ERR_BAD_ARG, sys_data_provider_send(7, (cmd_in_t){NULL, 0}, 0));
  ASSERT_ERR(ERR_BAD_ARG, sys_data_provider_send(7, (cmd_in_t){a, 0}, 0));
  ASSERT_ERR(ERR_BAD_ARG, sys_data_provider_send(7, (cmd_in_t){NULL, 1}, 0));
  TEST_ASSERT_EQUAL_UINT32(0, f->sends);
}

static void test_a_frame_past_the_limit_is_refused_never_cut(void) {
  fake_t* f = fake_add(0, 7, 0);
  static uint8_t big[CMD_FRAME_MAX + 1];
  TEST_ASSERT_NULL(sys_data_provider_send(7, (cmd_in_t){big, CMD_FRAME_MAX}, 0));  // exactly the limit
  TEST_ASSERT_EQUAL_UINT32(CMD_FRAME_MAX, f->queue[0].len);
  err_h e = sys_data_provider_send(7, (cmd_in_t){big, CMD_FRAME_MAX + 1}, 0);
  TEST_ASSERT_ERR_TAG(ERR_PROV_TOO_LONG, e);
  TEST_ASSERT_EQUAL_UINT16(CMD_FRAME_MAX + 1, ERR_DATA(e, ERR_PROV_TOO_LONG)->len);
  TEST_ASSERT_EQUAL_UINT16(CMD_FRAME_MAX, ERR_DATA(e, ERR_PROV_TOO_LONG)->max);
  err_free(e);
  ASSERT_ERR(ERR_PROV_TOO_LONG, sys_data_provider_send(7, (cmd_in_t){big, UINT32_MAX}, 0));
  TEST_ASSERT_EQUAL_UINT32(1, f->sends);  // the driver saw only the first
}

static void test_no_provider_or_one_down_is_not_sent(void) {
  fake_t* f = fake_add(0, 7, 0);
  const uint8_t a[] = {0x11};
  ASSERT_ERR(ERR_PROV_NOT_FOUND, sys_data_provider_send(8, (cmd_in_t){a, 1}, 0));
  sys_data_provider_set_up(f->p, false);
  ASSERT_ERR(ERR_PROV_DOWN, sys_data_provider_send(7, (cmd_in_t){a, 1}, 0));
  TEST_ASSERT_EQUAL_UINT32(0, f->sends);
}

static void test_busy_passes_as_it_is_other_driver_errors_are_wrapped(void) {
  fake_t* f = fake_add(0, 7, 0);
  const uint8_t a[] = {0x11};
  f->queued = QUEUE_FRAMES;
  ASSERT_ERR(ERR_PROV_BUSY, sys_data_provider_send(7, (cmd_in_t){a, 1}, CMD_SEND_NOWAIT));
  f->queued = 0;
  f->fail = ERR_NEW(ERR_CMD_BAD_LEN, .got = 1, .need = 2);
  err_h e = sys_data_provider_send(7, (cmd_in_t){a, 1}, 0);
  TEST_ASSERT_ERR_TAG(ERR_PROV_SEND, e);
  TEST_ASSERT_EQUAL_UINT8(7, ERR_DATA(e, ERR_PROV_SEND)->provider);
  TEST_ASSERT_NOT_NULL(err_cause(e));
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_BAD_LEN, err_tag(err_cause(e)));
  err_free(e);
}

static void test_a_provider_is_not_removed_while_its_driver_sends(void) {
  fake_t* f = fake_add(0, 7, 0);
  const uint8_t a[] = {0x11};
  f->remove_in_send = true;
  TEST_ASSERT_NULL(sys_data_provider_send(7, (cmd_in_t){a, 1}, 0));
  TEST_ASSERT_ERR_TAG(ERR_PROV_IN_USE, f->removed_in_send);
  err_free(f->removed_in_send);
  TEST_ASSERT_NOT_EQUAL(0, sys_data_provider_state(7));
  TEST_ASSERT_NULL(fake_remove_id(7));  // the send is over
  TEST_ASSERT_EQUAL_UINT8(0, sys_data_provider_state(7));
}

/* ---- SPARE: half of a queue kept for what may not be lost ----------------------------------------------------- */

static void test_spare_is_refused_past_half_the_queue(void) {
  TEST_ASSERT_FALSE(sys_data_provider_spare_refused(0, 3, 1, 4));                              // not SPARE
  TEST_ASSERT_FALSE(sys_data_provider_spare_refused(CMD_SEND_SPARE, 0, 1, 4));
  TEST_ASSERT_FALSE(sys_data_provider_spare_refused(CMD_SEND_SPARE, 1, 1, 4));   // to half: taken
  TEST_ASSERT_TRUE(sys_data_provider_spare_refused(CMD_SEND_SPARE, 2, 1, 4));    // past half
  TEST_ASSERT_TRUE(sys_data_provider_spare_refused(CMD_SEND_SPARE, 0, 600, 1024));  // bytes: too big
  TEST_ASSERT_FALSE(sys_data_provider_spare_refused(CMD_SEND_SPARE, 100, 412, 1024));
  TEST_ASSERT_TRUE(sys_data_provider_spare_refused(CMD_SEND_SPARE, 0, 1, 1));    // one frame: none spare
}

static void test_spare_frames_leave_room_for_the_answers(void) {
  fake_t* f = fake_add(0, 7, 0);
  const uint8_t push[] = {0x50};
  const uint8_t answer[] = {0x41};
  const uint8_t spare = CMD_SEND_SPARE | CMD_SEND_NOWAIT;
  TEST_ASSERT_NULL(sys_data_provider_send(7, (cmd_in_t){push, 1}, spare));
  TEST_ASSERT_NULL(sys_data_provider_send(7, (cmd_in_t){push, 1}, spare));
  ASSERT_ERR(ERR_PROV_BUSY, sys_data_provider_send(7, (cmd_in_t){push, 1}, spare));  // half the queue is theirs
  TEST_ASSERT_NULL(sys_data_provider_send(7, (cmd_in_t){answer, 1}, CMD_SEND_NOWAIT));
  TEST_ASSERT_NULL(sys_data_provider_send(7, (cmd_in_t){answer, 1}, CMD_SEND_NOWAIT));
  ASSERT_ERR(ERR_PROV_BUSY, sys_data_provider_send(7, (cmd_in_t){answer, 1}, CMD_SEND_NOWAIT));  // full
  TEST_ASSERT_EQUAL_UINT32(QUEUE_FRAMES, f->queued);
}

/* ---- the rx hook: one owner ----------------------------------------------------------------------------------- */

static void test_the_rx_hook_has_one_owner(void) {
  fake_add(0, 7, 0);
  TEST_ASSERT_NULL(sys_data_provider_open(7, user_rx, NULL));
  TEST_ASSERT_TRUE((sys_data_provider_state(7) & SYS_DATA_PROVIDER_STATE_OPEN) != 0);
  ASSERT_ERR(ERR_PROV_IN_USE, sys_data_provider_open(7, user_rx, NULL));
  ASSERT_ERR(ERR_PROV_IN_USE, fake_remove_id(7));  // open: not removed
  TEST_ASSERT_NULL(sys_data_provider_close(7));
  TEST_ASSERT_NULL(sys_data_provider_open(7, user_rx, NULL));  // free again
  ASSERT_ERR(ERR_PROV_NOT_FOUND, sys_data_provider_open(9, user_rx, NULL));
  ASSERT_ERR(ERR_PROV_NOT_FOUND, sys_data_provider_close(9));
}

static void test_up_down_is_told_once_per_change_and_frames_reach_the_owner(void) {
  fake_t* f = fake_add(0, 7, 0);
  const uint8_t in[] = {0x01, 0x02, 0x03};
  sys_data_provider_deliver(f->p, in, sizeof(in));  // no owner: dropped
  TEST_ASSERT_EQUAL_UINT32(0, s_rx.calls);
  TEST_ASSERT_NULL(sys_data_provider_open(7, user_rx, NULL));
  sys_data_provider_set_up(f->p, true);  // up already: nothing
  TEST_ASSERT_EQUAL_UINT32(0, s_rx.calls);
  sys_data_provider_set_up(f->p, false);
  TEST_ASSERT_EQUAL_UINT32(1, s_rx.calls);
  TEST_ASSERT_TRUE(s_rx.changed);
  TEST_ASSERT_EQUAL_UINT32(0, s_rx.len);
  TEST_ASSERT_FALSE((sys_data_provider_state(7) & SYS_DATA_PROVIDER_STATE_UP) != 0);
  sys_data_provider_set_up(f->p, true);
  TEST_ASSERT_EQUAL_UINT32(2, s_rx.calls);
  TEST_ASSERT_EQUAL_UINT32(1, s_rx.len);
  sys_data_provider_deliver(f->p, in, sizeof(in));
  TEST_ASSERT_EQUAL_UINT32(3, s_rx.calls);
  TEST_ASSERT_FALSE(s_rx.changed);
  TEST_ASSERT_EQUAL_UINT8(7, s_rx.id);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(in, s_rx.data, sizeof(in));
}

/* ---- CREATE and REMOVE ---------------------------------------------------------------------------------------- */

static void test_ids_are_unique_and_the_slots_are_counted(void) {
  fake_add(0, 1, 0);
  const fake_prov_cfg_t dummy_cfg = {.fake = (uintptr_t)&s_fake[CONFIG_SYS_DATA_PROVIDER_MAX]};
  ASSERT_ERR(ERR_PROV_ID_TAKEN, fake_prov_create(1, SYS_DATA_PROVIDER_TYPE_UART, 0, &dummy_cfg));
  for (uint32_t i = 1; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    fake_add(i, (uint8_t)(10 + i), 0);
  }
  ASSERT_ERR(ERR_FULL, fake_prov_create(99, SYS_DATA_PROVIDER_TYPE_UART, 0, &dummy_cfg));

  // Free a slot and verify that a buggy driver not setting p->send is rejected cleanly without leaking the slot.
  TEST_ASSERT_NULL(fake_remove_id(1));
  const fake_prov_cfg_t no_send_cfg = {.no_send = true};
  ASSERT_ERR(ERR_BAD_ARG, fake_prov_create(50, SYS_DATA_PROVIDER_TYPE_UART, 0, &no_send_cfg));
  TEST_ASSERT_EQUAL_UINT8(0, sys_data_provider_state(50));
}

static void test_the_boards_own_provider_stays_others_can_be_removed(void) {
  (void)fake_add_flags(0, 1, SYS_DATA_PROVIDER_CREATE_SYSTEM, 0);
  (void)fake_add(1, 2, 0);
  ASSERT_ERR(ERR_PROV_SYSTEM, fake_remove_id(1));
  TEST_ASSERT_NOT_EQUAL(0, sys_data_provider_state(1));
  TEST_ASSERT_NULL(fake_remove_id(2));
  TEST_ASSERT_EQUAL_UINT8(0, sys_data_provider_state(2));  // its id finds nothing
  ASSERT_ERR(ERR_PROV_NOT_FOUND, fake_remove_id(2));
}

/* ---- Typed MQTT mock: router dispatch, INFO/LIST/OPS, own k_ops (0xFFxx), LOCAL check ------------------------- */

#define PROV_MQTT_OP_TOPIC SYS_DATA_PROVIDER_OWN_OP(0x01)
#define PROV_MQTT_OP_LOCAL SYS_DATA_PROVIDER_OWN_OP(0x02)

typedef struct __attribute__((packed)) {
  uint8_t mode;
  uint8_t fail;
} mock_prov_cfg_t;

typedef struct __attribute__((packed)) {
  uint32_t topic;
} mock_topic_t;

typedef struct {
  sys_data_provider_t* prov;
  uint8_t mode;
  uint32_t topic;
  uint32_t sent;
  uint8_t last[64];
  uint32_t last_len;
} mock_prov_ctx_t;

static mock_prov_ctx_t* s_last_mock;
static uint32_t s_remove_calls;
static uint32_t s_local_calls;

static err_h mock_prov_send(sys_data_provider_t* p, cmd_in_t frame, uint8_t flags) {
  (void)flags;
  mock_prov_ctx_t* ctx = p->driver;
  const uint32_t n = frame.len <= sizeof(ctx->last) ? frame.len : sizeof(ctx->last);
  if (n > 0) {
    memcpy(ctx->last, frame.data, n);
  }
  ctx->last_len = n;
  ctx->sent++;
  return NULL;
}

static err_h mock_prov_create(sys_data_provider_t* p, void* ctx_in, const void* cfg_in) {
  mock_prov_ctx_t* ctx = ctx_in;
  const mock_prov_cfg_t* cfg = cfg_in;
  if (cfg->fail != 0) {
    return ERR_NEW(ERR_BAD_ARG);
  }
  ctx->prov = p;
  ctx->mode = cfg->mode;
  p->send = mock_prov_send;
  p->state |= SYS_DATA_PROVIDER_STATE_WIRED | SYS_DATA_PROVIDER_STATE_FRAMED | SYS_DATA_PROVIDER_STATE_UP;
  s_last_mock = ctx;
  return NULL;
}

static err_h mock_prov_remove(sys_data_provider_t* p, void* ctx_in) {
  (void)p;
  (void)ctx_in;
  s_remove_calls++;
  return NULL;
}

static err_h mock_op_topic_read(void* ctx_in, cmd_in_t in, cmd_out_t* out) {
  ERR_CHECK(in.len == 0, ERR_CMD_BAD_LEN, .got = (uint16_t)in.len, .need = 0);
  const mock_prov_ctx_t* c = ctx_in;
  const mock_topic_t v = {.topic = c->topic};
  return cmd_reply(out, &v, sizeof(v));
}

static err_h mock_op_topic_write(void* ctx_in, cmd_in_t in, cmd_out_t* out) {
  (void)out;
  ERR_CHECK(in.len == sizeof(mock_topic_t), ERR_CMD_BAD_LEN, .got = (uint16_t)in.len, .need = sizeof(mock_topic_t));
  mock_prov_ctx_t* c = ctx_in;
  mock_topic_t v;
  memcpy(&v, in.data, sizeof(v));
  c->topic = v.topic;
  return NULL;
}

static err_h mock_op_local_write(void* ctx_in, cmd_in_t in, cmd_out_t* out) {
  (void)ctx_in;
  (void)out;
  ERR_CHECK(in.len == 0, ERR_CMD_BAD_LEN, .got = (uint16_t)in.len, .need = 0);
  s_local_calls++;
  return NULL;
}

static const sys_data_provider_op_t k_mock_ops[] = {
    {.op = PROV_MQTT_OP_TOPIC, .read = mock_op_topic_read, .write = mock_op_topic_write, .flags = 0},
    {.op = PROV_MQTT_OP_LOCAL, .read = NULL, .write = mock_op_local_write, .flags = SYS_DATA_PROVIDER_OP_LOCAL},
};

SYS_DATA_PROVIDER_TYPE_DEFINE(SYS_DATA_PROVIDER_TYPE_MQTT, mock_prov_cfg_t, mock_prov_ctx_t, mock_prov_create,
                              mock_prov_remove, k_mock_ops, ARRAY_LEN(k_mock_ops))

static err_h mock_router_call(uint8_t dir, uint8_t id, uint16_t op, const void* args, uint32_t args_len,
                              cmd_out_t* out) {
  uint8_t frame[128];
  frame[0] = MODULE_ID_PROVIDER;
  const sys_data_provider_router_hdr_t h = {.dir = dir, .id = id, .op = op};
  memcpy(frame + 1, &h, sizeof(h));
  if (args_len > 0) {
    memcpy(frame + 1 + sizeof(h), args, args_len);
  }
  const cmd_in_t in = {.data = frame, .len = 1u + sizeof(h) + args_len};
  return sys_data_provider_router(NULL, in, out);
}

static err_h mock_create(uint8_t id, uint8_t type, uint8_t flags, const mock_prov_cfg_t* cfg) {
  uint8_t buf[SYS_DATA_PROVIDER_CREATE_HDR + sizeof(mock_prov_cfg_t)] = {type, flags};
  memcpy(buf + SYS_DATA_PROVIDER_CREATE_HDR, cfg, sizeof(*cfg));
  return mock_router_call(CMD_DIR_WRITE, id, CONTRACT_PROVIDER_OP_CREATE, buf, sizeof(buf), NULL);
}

static void test_typed_create_info_ops_list_send_own_op_and_remove(void) {
  s_remove_calls = 0;
  s_local_calls = 0;
  const mock_prov_cfg_t cfg = {.mode = 3};
  TEST_ASSERT_NULL(mock_create(20, SYS_DATA_PROVIDER_TYPE_MQTT, 0, &cfg));

  sys_data_provider_info_t info = {0};
  TEST_ASSERT_NULL(sys_data_provider_read(20, CONTRACT_PROVIDER_OP_INFO, NULL, 0, &info, sizeof(info)));
  TEST_ASSERT_EQUAL_UINT8(SYS_DATA_PROVIDER_TYPE_MQTT, info.type);
  TEST_ASSERT_EQUAL_UINT8(SYS_DATA_PROVIDER_STATE_WIRED | SYS_DATA_PROVIDER_STATE_FRAMED | SYS_DATA_PROVIDER_STATE_UP,
                          info.state);

  // LIST requires ID_NONE; INFO requires instance ID
  err_h bad_list = sys_data_provider_call(20, CMD_DIR_READ, CONTRACT_PROVIDER_OP_LIST, (cmd_in_t){0}, NULL);
  TEST_ASSERT_EQUAL_HEX32(ERR_PROV_BAD_ID, err_root_tag(bad_list));
  err_free(bad_list);
  err_h bad_info = sys_data_provider_call(SYS_DATA_PROVIDER_ID_NONE, CMD_DIR_READ, CONTRACT_PROVIDER_OP_INFO,
                                          (cmd_in_t){0}, NULL);
  TEST_ASSERT_EQUAL_HEX32(ERR_PROV_BAD_ID, err_root_tag(bad_info));
  err_free(bad_info);

  // LIST via router on ID_NONE returns count 1 + 3-byte row
  uint8_t list_buf[64];
  cmd_out_t list_out = {.data = list_buf, .cap = sizeof(list_buf)};
  TEST_ASSERT_NULL(
      mock_router_call(CMD_DIR_READ, SYS_DATA_PROVIDER_ID_NONE, CONTRACT_PROVIDER_OP_LIST, NULL, 0, &list_out));
  TEST_ASSERT_EQUAL_UINT32(1u + sizeof(sys_data_provider_entry_t), list_out.len);
  TEST_ASSERT_EQUAL_UINT8(1, list_buf[0]);
  sys_data_provider_entry_t row;
  memcpy(&row, list_buf + 1, sizeof(row));
  TEST_ASSERT_EQUAL_UINT8(20, row.provider);
  TEST_ASSERT_EQUAL_UINT8(SYS_DATA_PROVIDER_TYPE_MQTT, row.type);

  // OPS includes non-LOCAL own op (PROV_MQTT_OP_TOPIC) and omits PROV_MQTT_OP_LOCAL
  uint8_t ops_buf[64];
  cmd_out_t ops_out = {.data = ops_buf, .cap = sizeof(ops_buf)};
  TEST_ASSERT_NULL(mock_router_call(CMD_DIR_READ, 20, CONTRACT_PROVIDER_OP_OPS, NULL, 0, &ops_out));
  bool saw_topic = false;
  bool saw_local = false;
  for (uint32_t i = 0; i + 3u <= ops_out.len; i += 3u) {
    uint16_t op_id;
    memcpy(&op_id, ops_buf + i, sizeof(op_id));
    if (op_id == PROV_MQTT_OP_TOPIC) {
      saw_topic = true;
      TEST_ASSERT_EQUAL_UINT8(0x03u, ops_buf[i + 2u]);  // read + write
    } else if (op_id == PROV_MQTT_OP_LOCAL) {
      saw_local = true;
    }
  }
  TEST_ASSERT_TRUE(saw_topic);
  TEST_ASSERT_FALSE(saw_local);

  // Own op write + read
  const mock_topic_t set_topic = {.topic = 0x12345678u};
  TEST_ASSERT_NULL(sys_data_provider_write(20, PROV_MQTT_OP_TOPIC, &set_topic, sizeof(set_topic)));
  mock_topic_t got = {0};
  TEST_ASSERT_NULL(sys_data_provider_read(20, PROV_MQTT_OP_TOPIC, NULL, 0, &got, sizeof(got)));
  TEST_ASSERT_EQUAL_UINT32(0x12345678u, got.topic);

  // LOCAL op refused via router, accepted in C
  err_h local_err = mock_router_call(CMD_DIR_WRITE, 20, PROV_MQTT_OP_LOCAL, NULL, 0, NULL);
  TEST_ASSERT_EQUAL_HEX32(ERR_PROV_LOCAL_OP, err_root_tag(local_err));
  err_free(local_err);
  TEST_ASSERT_NULL(sys_data_provider_write(20, PROV_MQTT_OP_LOCAL, NULL, 0));
  TEST_ASSERT_EQUAL_UINT32(1, s_local_calls);

  // Router OP_SEND
  const uint8_t payload[] = {0xAA, 0xBB, 0xCC};
  TEST_ASSERT_NULL(mock_router_call(CMD_DIR_WRITE, 20, CONTRACT_PROVIDER_OP_SEND, payload, sizeof(payload), NULL));
  TEST_ASSERT_EQUAL_UINT32(1, s_last_mock->sent);
  TEST_ASSERT_EQUAL_UINT32(sizeof(payload), s_last_mock->last_len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, s_last_mock->last, sizeof(payload));

  // Driver deliver + set_up helpers
  TEST_ASSERT_NULL(sys_data_provider_open(20, user_rx, NULL));
  sys_data_provider_deliver(s_last_mock->prov, payload, sizeof(payload));
  TEST_ASSERT_EQUAL_UINT32(1, s_rx.calls);
  sys_data_provider_set_up(s_last_mock->prov, false);
  TEST_ASSERT_FALSE((sys_data_provider_state(20) & SYS_DATA_PROVIDER_STATE_UP) != 0);
  sys_data_provider_set_up(s_last_mock->prov, true);
  TEST_ASSERT_TRUE((sys_data_provider_state(20) & SYS_DATA_PROVIDER_STATE_UP) != 0);

  // REMOVE refused while open, succeeds after close and calls remove callback
  err_h in_use = mock_router_call(CMD_DIR_WRITE, 20, CONTRACT_PROVIDER_OP_REMOVE, NULL, 0, NULL);
  TEST_ASSERT_EQUAL_HEX32(ERR_PROV_IN_USE, err_root_tag(in_use));
  err_free(in_use);
  TEST_ASSERT_NULL(sys_data_provider_close(20));

  TEST_ASSERT_NULL(mock_router_call(CMD_DIR_WRITE, 20, CONTRACT_PROVIDER_OP_REMOVE, NULL, 0, NULL));
  TEST_ASSERT_EQUAL_UINT8(0, sys_data_provider_state(20));
  TEST_ASSERT_EQUAL_UINT32(1, s_remove_calls);
}

static void test_typed_create_failure_unknown_type_and_slot_reuse(void) {
  const mock_prov_cfg_t fail_cfg = {.fail = 1};
  err_h e = mock_create(21, SYS_DATA_PROVIDER_TYPE_MQTT, 0, &fail_cfg);
  TEST_ASSERT_ERR_TAG(ERR_BAD_ARG, e);
  err_free(e);
  TEST_ASSERT_EQUAL_UINT8(0, sys_data_provider_state(21));

  // Unknown type
  err_h unk = mock_create(21, 0x99u, 0, &fail_cfg);
  TEST_ASSERT_EQUAL_HEX32(ERR_PROV_TYPE_UNKNOWN, err_root_tag(unk));
  err_free(unk);

  // Unknown OP (both 0xFFxx and 0x2Bxx) + bad args len on empty-input op
  const mock_prov_cfg_t ok_cfg = {.mode = 1};
  TEST_ASSERT_NULL(mock_create(21, SYS_DATA_PROVIDER_TYPE_MQTT, 0, &ok_cfg));
  err_h u_op = mock_router_call(CMD_DIR_READ, 21, 0xFF99u, NULL, 0, NULL);
  TEST_ASSERT_EQUAL_HEX32(ERR_PROV_UNKNOWN_OP, err_root_tag(u_op));
  err_free(u_op);
  err_h u_reg = mock_router_call(CMD_DIR_WRITE, 21, (uint16_t)((MODULE_ID_PROVIDER << 8) | 0x99u), NULL, 0, NULL);
  TEST_ASSERT_EQUAL_HEX32(ERR_PROV_UNKNOWN_OP, err_root_tag(u_reg));
  err_free(u_reg);
  const uint8_t junk = 1u;
  err_h bad_len = mock_router_call(CMD_DIR_READ, 21, CONTRACT_PROVIDER_OP_INFO, &junk, sizeof(junk), NULL);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_BAD_LEN, err_root_tag(bad_len));
  err_free(bad_len);
  TEST_ASSERT_NULL(fake_remove_id(21));
}

static void test_typed_create_system_cannot_be_removed(void) {
  const mock_prov_cfg_t cfg = {.mode = 1};
  TEST_ASSERT_NULL(mock_create(30, SYS_DATA_PROVIDER_TYPE_MQTT, SYS_DATA_PROVIDER_CREATE_SYSTEM, &cfg));
  err_h e = mock_router_call(CMD_DIR_WRITE, 30, CONTRACT_PROVIDER_OP_REMOVE, NULL, 0, NULL);
  TEST_ASSERT_EQUAL_HEX32(ERR_PROV_SYSTEM, err_root_tag(e));
  err_free(e);
  s_last_mock->prov->state &= (uint8_t)~SYS_DATA_PROVIDER_STATE_SYSTEM;
  TEST_ASSERT_NULL(fake_remove_id(30));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_flat_frame_and_flags_reach_the_driver);
  RUN_TEST(test_an_empty_frame_or_null_bytes_is_refused);
  RUN_TEST(test_a_frame_past_the_limit_is_refused_never_cut);
  RUN_TEST(test_no_provider_or_one_down_is_not_sent);
  RUN_TEST(test_busy_passes_as_it_is_other_driver_errors_are_wrapped);
  RUN_TEST(test_a_provider_is_not_removed_while_its_driver_sends);
  RUN_TEST(test_spare_is_refused_past_half_the_queue);
  RUN_TEST(test_spare_frames_leave_room_for_the_answers);
  RUN_TEST(test_the_rx_hook_has_one_owner);
  RUN_TEST(test_up_down_is_told_once_per_change_and_frames_reach_the_owner);
  RUN_TEST(test_ids_are_unique_and_the_slots_are_counted);
  RUN_TEST(test_the_boards_own_provider_stays_others_can_be_removed);
  RUN_TEST(test_typed_create_info_ops_list_send_own_op_and_remove);
  RUN_TEST(test_typed_create_failure_unknown_type_and_slot_reuse);
  RUN_TEST(test_typed_create_system_cannot_be_removed);
  return UNITY_END();
}
