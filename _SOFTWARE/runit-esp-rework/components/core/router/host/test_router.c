/* Host tests of the router's routing: the root table, a module's command table and the handlers written from it
 * (router_table.h). The real router and cmd and errors; the queue and the tasks are in test_router_queue.c / test_router_tasks.c.
 * Run: python test_host/run.py router   (runit-esp/test_host/HOST_TESTS.MD) */
#include <string.h>
#include "unity.h"
#include "host_assert.h"
#include "router_table.h"
#include "host_mocks.h"
#include "utils.h"

/* ---- a module the tests route to: its table and handlers, as a module writes them ---------------------------- */

typedef struct __attribute__((packed)) {
  uint8_t a;
  uint16_t b;
} pair_args_t;

typedef struct __attribute__((packed)) {
  uint8_t id;
} id_args_t;

#define TEST_OPERATIONS(X, P)                             \
  X(P, OP_PAIR,      0x01, pair_args_t, pair_args_t, 0)   \
  X(P, OP_PING,      0x02, cmd_none_t,  uint8_t,     0)   \
  X(P, OP_NAME,      0x03, cmd_none_t,  id_args_t,   0)   \
  X(P, OP_FAIL,      0x04, id_args_t,   id_args_t,   0)   \
  X(P, OP_PARTIAL,   0x05, cmd_none_t,  cmd_none_t,  0)   \
  X(P, OP_READONLY,  0x06, cmd_none_t,  uint8_t,     0)   \
  X(P, OP_WRITEONLY, 0x07, cmd_none_t,  pair_args_t, 0)

CMD_TABLE_DECLARE(TEST)

static struct {
  void* ctx;
  uint8_t a;
  uint16_t b;
  uint8_t tail_len;
  uint8_t tail0;
  int calls;
} g;

static err_h pair(void* ctx, const pair_args_t* args, cmd_out_t* out) {
  g.calls++;
  g.ctx = ctx;
  g.a = args->a;
  g.b = args->b;
  return cmd_reply(out, &args->b, sizeof(args->b));
}

static err_h ping(void* ctx, cmd_out_t* out) {
  g.calls++;
  g.ctx = ctx;
  return CMD_REPLY(TEST, OP_PING, 0x55);
}

ROUTER_READ(TEST, OP_PAIR) {
  return pair(c, args, out);
}

ROUTER_WRITE(TEST, OP_PAIR) {
  return pair(c, args, out);
}

ROUTER_READ(TEST, OP_PING) {
  return ping(c, out);
}

ROUTER_WRITE_TAIL(TEST, OP_NAME) {
  g.calls++;
  g.a = args->id;
  g.tail_len = (uint8_t)tail_len;
  g.tail0 = tail_len > 0 ? tail[0] : 0;
  return cmd_reply(out, tail, tail_len);
}

ROUTER_READ(TEST, OP_FAIL) {
  ERR_CHECK(args->id != 0, ERR_CMD_BAD_ARG);  /* id 0 is refused */
  return NULL;
}

ROUTER_WRITE(TEST, OP_FAIL) {
  ERR_CHECK(args->id != 0, ERR_CMD_BAD_ARG);
  return NULL;
}

static err_h TEST_r_OP_PARTIAL(CMD_F_PARAMS) {
  (void)ctx;
  (void)in;
  const uint8_t half[] = {1, 2, 3};
  ERR_TRY(cmd_reply(out, half, sizeof(half)));
  return ERR_NEW(ERR_CMD_BAD_ARG);
}

ROUTER_READ(TEST, OP_READONLY) {
  return ping(c, out);
}

ROUTER_WRITE(TEST, OP_WRITEONLY) {
  return pair(c, args, out);
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY(TEST, OP_PAIR),        ROUTER_ENTRY_R(TEST, OP_PING),    ROUTER_ENTRY_W(TEST, OP_NAME),
    ROUTER_ENTRY(TEST, OP_FAIL),        ROUTER_ENTRY_R(TEST, OP_PARTIAL), ROUTER_ENTRY_R(TEST, OP_READONLY),
    ROUTER_ENTRY_W(TEST, OP_WRITEONLY),
};
static ROUTER_MODULE(mod_router, k_ops)

MODULE_ID_DECLARE(TEST_MOD,  0x21)
MODULE_ID_DECLARE(TEST_ROOT, 0x22)
#define KEY_MOD  MODULE_ID_TEST_MOD
#define KEY_ROOT MODULE_ID_TEST_ROOT  /* a plain handler registered at the root */

ROUTER_MODULE_REGISTER(TEST_MOD, mod_router)

static err_h root_handler(CMD_F_PARAMS) {
  g.calls++;
  g.ctx = ctx;
  g.tail_len = (uint8_t)in.len;
  g.tail0 = in.data[0];
  return cmd_reply(out, in.data, in.len);
}

ROUTER_MODULE_REGISTER(TEST_ROOT, root_handler)

static int s_registered;  /* keys of the root table so far (it cannot be emptied) */

void setUp(void) {
  host_log_clear();
  memset(&g, 0, sizeof(g));
  if (s_registered == 0) {
    TEST_ASSERT_NULL(router_root_init());
    s_registered = 2;
  }
}
void tearDown(void) {}

static cmd_out_t make_out(uint8_t* buf, uint32_t cap) {
  return (cmd_out_t){.data = buf, .cap = cap, .len = 0};
}

/* Runs [dir][code][args] through the module's table. */
static err_h call_op(void* ctx, const uint8_t* req, uint32_t len, cmd_out_t* out) {
  return router_call_op(k_ops, ARRAY_LEN(k_ops), ctx, (cmd_in_t){req, len}, out);
}

/* ---- the handlers of the table: a fixed frame (CMD_HANDLER) --------------------------------------------------- */

static void test_handler_gets_exactly_its_args(void) {
  uint8_t buf[16];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t req[] = {CMD_DIR_WRITE, TEST_OP_PAIR, 0x07, 0x34, 0x12};  /* [dir][code][a][b LE] */
  TEST_ASSERT_NULL(call_op(NULL, req, sizeof(req), &out));
  TEST_ASSERT_EQUAL_UINT8(7, g.a);
  TEST_ASSERT_EQUAL_HEX16(0x1234, g.b);
}

static void test_handler_refuses_a_payload_of_the_wrong_size(void) {
  uint8_t buf[16];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t req[] = {CMD_DIR_WRITE, TEST_OP_PAIR, 1, 2, 3, 4};
  err_h e = call_op(NULL, req, 4, &out);  /* one byte short */
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_OP, e);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_BAD_LEN, e->next->tag);
  TEST_ASSERT_EQUAL_UINT16(2, ERR_DATA(e->next, ERR_CMD_BAD_LEN)->got);  /* the payload's bytes, without [dir][code] */
  TEST_ASSERT_EQUAL_UINT16(3, ERR_DATA(e->next, ERR_CMD_BAD_LEN)->need);
  TEST_ASSERT_NULL(e->next->next);
  err_free(e);
  e = call_op(NULL, req, 6, &out);  /* one byte too many */
  TEST_ASSERT_EQUAL_UINT16(4, ERR_DATA(e->next, ERR_CMD_BAD_LEN)->got);
  err_free(e);
  e = call_op(NULL, req, 2, &out);  /* no payload at all */
  TEST_ASSERT_EQUAL_UINT16(0, ERR_DATA(e->next, ERR_CMD_BAD_LEN)->got);
  err_free(e);
  TEST_ASSERT_EQUAL_INT(0, g.calls);  /* the body never ran */
}

static void test_handler_of_no_args_refuses_a_byte_after_the_code(void) {
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t ok[] = {CMD_DIR_READ, TEST_OP_PING};
  TEST_ASSERT_NULL(call_op(NULL, ok, sizeof(ok), &out));
  const uint8_t extra[] = {CMD_DIR_READ, TEST_OP_PING, 1};
  err_h e = call_op(NULL, extra, sizeof(extra), &out);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_BAD_LEN, e->next->tag);
  TEST_ASSERT_EQUAL_UINT16(0, ERR_DATA(e->next, ERR_CMD_BAD_LEN)->need);
  err_free(e);
}

/* ---- a head and a tail (ROUTER_WRITE_TAIL) -------------------------------------------------------------------- */

static void test_tail_splits_the_head_and_the_bytes_after_it(void) {
  uint8_t buf[16];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t req[] = {CMD_DIR_WRITE, TEST_OP_NAME, 0x09, 'x', 'y'};
  TEST_ASSERT_NULL(call_op(NULL, req, sizeof(req), &out));
  TEST_ASSERT_EQUAL_UINT8(9, g.a);
  TEST_ASSERT_EQUAL_UINT8(2, g.tail_len);
  TEST_ASSERT_EQUAL_UINT8('x', g.tail0);
  TEST_ASSERT_EQUAL_UINT32(2, out.len);
}

static void test_tail_may_be_empty_and_the_head_is_needed(void) {
  uint8_t buf[16];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t head_only[] = {CMD_DIR_WRITE, TEST_OP_NAME, 0x09};
  TEST_ASSERT_NULL(call_op(NULL, head_only, sizeof(head_only), &out));  /* what the tail must be is the body's */
  TEST_ASSERT_EQUAL_UINT8(0, g.tail_len);
  const uint8_t no_head[] = {CMD_DIR_WRITE, TEST_OP_NAME};
  err_h e = call_op(NULL, no_head, sizeof(no_head), &out);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_BAD_LEN, e->next->tag);
  TEST_ASSERT_EQUAL_UINT16(0, ERR_DATA(e->next, ERR_CMD_BAD_LEN)->got);
  TEST_ASSERT_EQUAL_UINT16(1, ERR_DATA(e->next, ERR_CMD_BAD_LEN)->need);
  err_free(e);
}

/* ---- router_call_op: a module's command table ------------------------------------------------------------------- */

static void test_op_runs_the_side_the_direction_asks_for(void) {
  uint8_t buf[16];
  cmd_out_t out = make_out(buf, sizeof(buf));
  int ctx = 0;
  const uint8_t write[] = {CMD_DIR_WRITE, TEST_OP_WRITEONLY, 0x07, 0x34, 0x12};
  TEST_ASSERT_NULL(call_op(&ctx, write, sizeof(write), &out));
  TEST_ASSERT_EQUAL_INT(1, g.calls);
  TEST_ASSERT_EQUAL_PTR(&ctx, g.ctx);  /* the caller's ctx goes through */
  TEST_ASSERT_EQUAL_UINT8(7, g.a);
  const uint8_t read[] = {CMD_DIR_READ, TEST_OP_PING};
  out.len = 0;
  TEST_ASSERT_NULL(call_op(NULL, read, sizeof(read), &out));
  TEST_ASSERT_EQUAL_UINT32(1, out.len);
  TEST_ASSERT_EQUAL_HEX8(0x55, buf[0]);
}

static void test_op_a_request_too_short_for_dir_and_code(void) {
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t one[] = {CMD_DIR_READ};
  err_h e = call_op(NULL, one, 1, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_NO_KEY, e);
  err_free(e);
  e = call_op(NULL, NULL, 0, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_NO_KEY, e);
  err_free(e);
}

static void test_op_unknown_code(void) {
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t req[] = {CMD_DIR_READ, 0x99};
  err_h e = call_op(NULL, req, sizeof(req), &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_UNKNOWN_OP, e);
  TEST_ASSERT_EQUAL_UINT8(0x99, ERR_DATA(e, ERR_ROUTER_UNKNOWN_OP)->code);
  TEST_ASSERT_NULL(e->next);  /* not wrapped: there was no op to name */
  err_free(e);
  TEST_ASSERT_EQUAL_INT(0, g.calls);
}

static void test_op_a_side_the_command_does_not_have(void) {
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t write_to_readonly[] = {CMD_DIR_WRITE, TEST_OP_READONLY};
  err_h e = call_op(NULL, write_to_readonly, 2, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_OP, e);  /* the route step wraps the cause */
  TEST_ASSERT_EQUAL_UINT8(TEST_OP_READONLY, ERR_DATA(e, ERR_ROUTER_OP)->code);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_DIRECTION, e->next->tag);
  TEST_ASSERT_EQUAL_UINT8(CMD_DIR_WRITE, ERR_DATA(e->next, ERR_CMD_DIRECTION)->dir);
  err_free(e);
  const uint8_t read_of_writeonly[] = {CMD_DIR_READ, TEST_OP_WRITEONLY};
  e = call_op(NULL, read_of_writeonly, 2, &out);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_DIRECTION, e->next->tag);
  err_free(e);
  const uint8_t odd_dir[] = {0x05, TEST_OP_PAIR};  /* both sides exist, the direction is neither */
  e = call_op(NULL, odd_dir, 2, &out);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_DIRECTION, e->next->tag);
  err_free(e);
  TEST_ASSERT_EQUAL_INT(0, g.calls);  /* no handler ran */
}

/* ---- router_call: the root table ------------------------------------------------------------------------------ */

static void test_call_empty_input_has_no_key(void) {
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  err_h e = router_call(NULL, 0, NULL, (cmd_in_t){NULL, 0}, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_NO_KEY, e);
  err_free(e);
}

static err_h h_seen_in(CMD_F_PARAMS) {
  g.ctx = ctx;
  g.tail_len = (uint8_t)in.len;
  g.tail0 = in.data[0];
  return cmd_reply(out, "k", 1);
}
static err_h h_fail(CMD_F_PARAMS) {
  (void)ctx, (void)in, (void)out;
  return ERR_NEW(ERR_CMD_BAD_ARG);
}

static void test_call_finds_the_key_and_passes_ctx_and_the_whole_input(void) {
  const router_entry_t table[] = {{0x10, h_fail}, {0x11, h_seen_in}};
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  int ctx = 0;
  const uint8_t in[] = {0x11, CMD_DIR_READ, 0x02};
  TEST_ASSERT_NULL(router_call(table, ARRAY_LEN(table), &ctx, (cmd_in_t){in, sizeof(in)}, &out));
  TEST_ASSERT_EQUAL_PTR(&ctx, g.ctx);
  TEST_ASSERT_EQUAL_UINT8(3, g.tail_len);  /* the key is still in front: the module router consumes it */
  TEST_ASSERT_EQUAL_UINT8(0x11, g.tail0);
}

static void test_call_unknown_key(void) {
  const router_entry_t table[] = {{0x10, h_fail}};
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t in[] = {0x77, 0};
  err_h e = router_call(table, 1, NULL, (cmd_in_t){in, sizeof(in)}, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_UNKNOWN_KEY, e);
  TEST_ASSERT_EQUAL_UINT8(0x77, ERR_DATA(e, ERR_ROUTER_UNKNOWN_KEY)->key);
  err_free(e);
}

static void test_call_wraps_a_failure_in_the_key(void) {
  const router_entry_t table[] = {{0x10, h_fail}};
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t in[] = {0x10, 0};
  err_h e = router_call(table, 1, NULL, (cmd_in_t){in, sizeof(in)}, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_KEY, e);
  TEST_ASSERT_EQUAL_UINT8(0x10, ERR_DATA(e, ERR_ROUTER_KEY)->key);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_BAD_ARG, e->next->tag);
  err_free(e);
}

/* ---- the whole route: root key -> module -> op ----------------------------------------------------------------- */

static void test_route_reaches_an_op_through_the_module_router(void) {
  uint8_t buf[CMD_DATA_MAX];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t req[] = {KEY_MOD, CMD_DIR_WRITE, TEST_OP_PAIR, 0x07, 0x34, 0x12};
  TEST_ASSERT_NULL(router_route((cmd_in_t){req, sizeof(req)}, &out));
  TEST_ASSERT_EQUAL_INT(1, g.calls);
  TEST_ASSERT_NULL(g.ctx);  /* the root passes ctx NULL */
  TEST_ASSERT_EQUAL_UINT8(7, g.a);
  TEST_ASSERT_EQUAL_UINT32(2, out.len);
  TEST_ASSERT_EQUAL_HEX8(0x34, buf[0]);
  TEST_ASSERT_EQUAL_HEX8(0x12, buf[1]);
}

static void test_route_a_failure_is_a_chain_of_the_steps_taken(void) {
  uint8_t buf[CMD_DATA_MAX];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t req[] = {KEY_MOD, CMD_DIR_WRITE, TEST_OP_FAIL, 0x00};  /* id 0: the op raises BAD_ARG */
  err_h e = router_route((cmd_in_t){req, sizeof(req)}, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_KEY, e);                    /* outermost: where it started */
  TEST_ASSERT_EQUAL_UINT8(KEY_MOD, ERR_DATA(e, ERR_ROUTER_KEY)->key);
  TEST_ASSERT_EQUAL_HEX32(ERR_ROUTER_OP, e->next->tag);
  TEST_ASSERT_EQUAL_UINT8(TEST_OP_FAIL, ERR_DATA(e->next, ERR_ROUTER_OP)->code);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_BAD_ARG, e->next->next->tag);       /* the cause */
  TEST_ASSERT_NULL(e->next->next->next);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_BAD_ARG, err_root_tag(e));
  TEST_ASSERT_EQUAL_UINT8(ERR_SEV_HIGH, err_chain_sev(e));            /* the route steps add no severity */
  err_free(e);
}

static void test_route_the_answer_is_empty_when_a_handler_fails_after_replying(void) {
  uint8_t buf[CMD_DATA_MAX];
  cmd_out_t out = make_out(buf, sizeof(buf));
  out.len = 99;  /* whatever was there */
  const uint8_t req[] = {KEY_MOD, CMD_DIR_READ, TEST_OP_PARTIAL};
  err_h e = router_route((cmd_in_t){req, sizeof(req)}, &out);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_UINT32(0, out.len);  /* the half answer is not an answer */
  err_free(e);
  const uint8_t ok[] = {KEY_ROOT, 0xAA};
  out.len = 99;
  TEST_ASSERT_NULL(router_route((cmd_in_t){ok, sizeof(ok)}, &out));
  TEST_ASSERT_EQUAL_UINT32(2, out.len);  /* and a fresh answer starts empty, not after the old length */
}

static void test_route_unknown_root_key_and_no_key(void) {
  uint8_t buf[8];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t req[] = {0x6F, CMD_DIR_READ};
  err_h e = router_route((cmd_in_t){req, sizeof(req)}, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_UNKNOWN_KEY, e);
  err_free(e);
  e = router_route((cmd_in_t){NULL, 0}, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_NO_KEY, e);
  err_free(e);
  const uint8_t key_only[] = {KEY_MOD};  /* a module with no [dir][code] */
  e = router_route((cmd_in_t){key_only, 1}, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_KEY, e);
  TEST_ASSERT_EQUAL_HEX32(ERR_ROUTER_NO_KEY, e->next->tag);
  err_free(e);
}

/* ---- router_register / router_root_init ---------------------------------------------------------------- */

static void test_register_refuses_key_zero_and_no_handler(void) {
  router_register(0, root_handler);
  err_h e = router_root_init();
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_ARG, e);
  err_free(e);
  router_register(0x50, NULL);
  e = router_root_init();
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_ARG, e);
  err_free(e);
  const uint8_t req[] = {0x50, 0};  /* and nothing was bound */
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  e = router_route((cmd_in_t){req, sizeof(req)}, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_UNKNOWN_KEY, e);
  err_free(e);
}

static void test_register_refuses_a_key_taken(void) {
  router_register(KEY_MOD, root_handler);
  err_h e = router_root_init();
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_KEY_TAKEN, e);
  TEST_ASSERT_EQUAL_UINT8(KEY_MOD, ERR_DATA(e, ERR_ROUTER_KEY_TAKEN)->key);
  err_free(e);
  const uint8_t req[] = {KEY_MOD, CMD_DIR_READ, TEST_OP_PING};  /* the first binding is kept */
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  TEST_ASSERT_NULL(router_route((cmd_in_t){req, sizeof(req)}, &out));
  TEST_ASSERT_EQUAL_HEX8(0x55, buf[0]);
}

static void test_register_a_full_table_refuses_one_more_and_keeps_the_rest(void) {
  uint8_t key = 0x40;
  err_h e = NULL;
  while (s_registered < CONFIG_ROUTER_MAX_KEYS) {
    router_register(key++, root_handler);
    TEST_ASSERT_NULL(router_root_init());
    s_registered++;
  }
  router_register(key, root_handler);
  e = router_root_init();
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_TABLE_FULL, e);
  TEST_ASSERT_EQUAL_UINT8(key, ERR_DATA(e, ERR_ROUTER_TABLE_FULL)->key);
  err_free(e);
  const uint8_t req[] = {KEY_ROOT, 0x01};  /* what was bound still routes */
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  TEST_ASSERT_NULL(router_route((cmd_in_t){req, sizeof(req)}, &out));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_handler_gets_exactly_its_args);
  RUN_TEST(test_handler_refuses_a_payload_of_the_wrong_size);
  RUN_TEST(test_handler_of_no_args_refuses_a_byte_after_the_code);
  RUN_TEST(test_tail_splits_the_head_and_the_bytes_after_it);
  RUN_TEST(test_tail_may_be_empty_and_the_head_is_needed);
  RUN_TEST(test_op_runs_the_side_the_direction_asks_for);
  RUN_TEST(test_op_a_request_too_short_for_dir_and_code);
  RUN_TEST(test_op_unknown_code);
  RUN_TEST(test_op_a_side_the_command_does_not_have);
  RUN_TEST(test_call_empty_input_has_no_key);
  RUN_TEST(test_call_finds_the_key_and_passes_ctx_and_the_whole_input);
  RUN_TEST(test_call_unknown_key);
  RUN_TEST(test_call_wraps_a_failure_in_the_key);
  RUN_TEST(test_route_reaches_an_op_through_the_module_router);
  RUN_TEST(test_route_a_failure_is_a_chain_of_the_steps_taken);
  RUN_TEST(test_route_the_answer_is_empty_when_a_handler_fails_after_replying);
  RUN_TEST(test_route_unknown_root_key_and_no_key);
  RUN_TEST(test_register_refuses_key_zero_and_no_handler);
  RUN_TEST(test_register_refuses_a_key_taken);
  RUN_TEST(test_register_a_full_table_refuses_one_more_and_keeps_the_rest);  /* last: it fills the table */
  return UNITY_END();
}
