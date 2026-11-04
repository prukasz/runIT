/* Host tests of the router's routing: the root table, a module's command table, the payload helpers and the op
 * macros. The real router and cmd and errors; the queue and the tasks are in test_router_queue.c / test_router_tasks.c.
 * Run: python test_host/run.py router   (runit-esp/test_host/HOST_TESTS.MD) */
#include <string.h>
#include "unity.h"
#include "host_assert.h"
#include "router.h"
#include "host_mocks.h"

/* ---- a module the tests route to ------------------------------------------------------------------------------ */

typedef struct __attribute__((packed)) {
  uint8_t a;
  uint16_t b;
} pair_args_t;

typedef struct __attribute__((packed)) {
  uint8_t id;
} id_args_t;

static struct {
  void* ctx;
  uint8_t a;
  uint16_t b;
  uint8_t tail_len;
  uint8_t tail0;
  int calls;
} g;

ROUTER_OP(op_pair, pair_args_t) {
  g.calls++;
  g.ctx = ctx;
  g.a = args->a;
  g.b = args->b;
  return cmd_reply(out, &args->b, sizeof(args->b));
}

ROUTER_OP_NOARGS(op_ping) {
  g.calls++;
  g.ctx = ctx;
  const uint8_t pong = 0x55;
  return cmd_reply(out, &pong, 1);
}

ROUTER_OP_TAIL(op_name, id_args_t) {
  (void)ctx;
  g.calls++;
  g.a = args->id;
  g.tail_len = (uint8_t)tail.len;
  g.tail0 = tail.data[0];
  return cmd_reply(out, tail.data, tail.len);
}

ROUTER_OP(op_fail, id_args_t) {
  (void)ctx;
  (void)out;
  ERR_CHECK(args->id != 0, ERR_CMD_BAD_ARG);  /* id 0 is the only good one */
  return NULL;
}

static err_h op_partial_then_fail(CMD_F_PARAMS) {
  (void)ctx;
  (void)in;
  const uint8_t half[] = {1, 2, 3};
  ERR_TRY(cmd_reply(out, half, sizeof(half)));
  return ERR_NEW(ERR_CMD_BAD_ARG);
}

#define CODE_PAIR    0x01
#define CODE_PING    0x02
#define CODE_NAME    0x03
#define CODE_FAIL    0x04
#define CODE_PARTIAL 0x05
#define CODE_READONLY 0x06
#define CODE_WRITEONLY 0x07

static const router_op_t k_ops[] = {
    ROUTER_RW(CODE_PAIR, op_pair, op_pair),
    ROUTER_R(CODE_PING, op_ping),
    ROUTER_W(CODE_NAME, op_name),
    ROUTER_RW(CODE_FAIL, op_fail, op_fail),
    ROUTER_R(CODE_PARTIAL, op_partial_then_fail),
    ROUTER_R(CODE_READONLY, op_ping),
    ROUTER_W(CODE_WRITEONLY, op_pair),
};
static ROUTER_MODULE(mod_router, k_ops)

#define KEY_MOD  0x21
#define KEY_ROOT 0x22  /* a plain handler registered at the root */

static err_h root_handler(CMD_F_PARAMS) {
  g.calls++;
  g.ctx = ctx;
  g.tail_len = (uint8_t)in.len;
  g.tail0 = in.data[0];
  return cmd_reply(out, in.data, in.len);
}

static int s_registered;  /* keys of the root table so far (it cannot be emptied) */

void setUp(void) {
  host_log_clear();
  memset(&g, 0, sizeof(g));
  if (s_registered == 0) {
    TEST_ASSERT_NULL(router_register(KEY_MOD, mod_router));
    TEST_ASSERT_NULL(router_register(KEY_ROOT, root_handler));
    s_registered = 2;
  }
}
void tearDown(void) {}

static cmd_out_t make_out(uint8_t* buf, uint32_t cap) {
  return (cmd_out_t){.data = buf, .cap = cap, .len = 0};
}

/* ---- router_args ---------------------------------------------------------------------------------------------- */

static void test_args_copies_exactly_the_payload_after_the_key(void) {
  const uint8_t in[] = {0xEE, 0x07, 0x34, 0x12};  /* [key][a][b LE] */
  pair_args_t args;
  TEST_ASSERT_NULL(router_args((cmd_in_t){in, sizeof(in)}, &args, sizeof(args)));
  TEST_ASSERT_EQUAL_UINT8(7, args.a);
  TEST_ASSERT_EQUAL_HEX16(0x1234, args.b);
}

static void test_args_refuses_a_payload_of_the_wrong_size(void) {
  const uint8_t in[] = {0xEE, 1, 2, 3, 4};
  pair_args_t args;
  err_h e = router_args((cmd_in_t){in, 3}, &args, sizeof(args));  /* one byte short */
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_LEN, e);
  TEST_ASSERT_EQUAL_UINT16(2, ERR_DATA(e, ERR_CMD_BAD_LEN)->got);
  TEST_ASSERT_EQUAL_UINT16(3, ERR_DATA(e, ERR_CMD_BAD_LEN)->need);
  err_free(e);
  e = router_args((cmd_in_t){in, 5}, &args, sizeof(args));  /* one byte too many */
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_LEN, e);
  TEST_ASSERT_EQUAL_UINT16(4, ERR_DATA(e, ERR_CMD_BAD_LEN)->got);
  err_free(e);
  e = router_args((cmd_in_t){NULL, 0}, &args, sizeof(args));  /* not even the key */
  TEST_ASSERT_EQUAL_UINT16(0, ERR_DATA(e, ERR_CMD_BAD_LEN)->got);
  err_free(e);
}

static void test_args_of_size_zero_only_checks_that_nothing_follows(void) {
  const uint8_t key_only[] = {0xEE};
  const uint8_t extra[] = {0xEE, 1};
  TEST_ASSERT_NULL(router_args((cmd_in_t){key_only, 1}, NULL, 0));
  TEST_ASSERT_NULL(router_args((cmd_in_t){NULL, 0}, NULL, 0));
  err_h e = router_args((cmd_in_t){extra, 2}, NULL, 0);
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_LEN, e);
  TEST_ASSERT_EQUAL_UINT16(0, ERR_DATA(e, ERR_CMD_BAD_LEN)->need);
  err_free(e);
}

/* ---- router_args_tail ----------------------------------------------------------------------------------------- */

static void test_args_tail_splits_header_and_tail(void) {
  const uint8_t in[] = {0xEE, 9, 'a', 'b', 'c'};
  id_args_t hdr;
  cmd_in_t tail;
  TEST_ASSERT_NULL(router_args_tail((cmd_in_t){in, sizeof(in)}, &hdr, sizeof(hdr), &tail));
  TEST_ASSERT_EQUAL_UINT8(9, hdr.id);
  TEST_ASSERT_EQUAL_UINT32(3, tail.len);
  TEST_ASSERT_EQUAL_PTR(in + 2, tail.data);  /* points into the request, no copy */
}

static void test_args_tail_needs_at_least_one_tail_byte(void) {
  const uint8_t in[] = {0xEE, 9};
  id_args_t hdr;
  cmd_in_t tail;
  err_h e = router_args_tail((cmd_in_t){in, 2}, &hdr, sizeof(hdr), &tail);  /* the header and nothing after */
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_LEN, e);
  TEST_ASSERT_EQUAL_UINT16(1, ERR_DATA(e, ERR_CMD_BAD_LEN)->got);
  TEST_ASSERT_EQUAL_UINT16(2, ERR_DATA(e, ERR_CMD_BAD_LEN)->need);  /* the header and one byte */
  err_free(e);
  e = router_args_tail((cmd_in_t){NULL, 0}, &hdr, sizeof(hdr), &tail);
  TEST_ASSERT_EQUAL_UINT16(0, ERR_DATA(e, ERR_CMD_BAD_LEN)->got);
  err_free(e);
}

static void test_args_tail_of_size_zero_is_all_tail(void) {
  const uint8_t in[] = {0xEE, 1, 2, 3};
  cmd_in_t tail;
  TEST_ASSERT_NULL(router_args_tail((cmd_in_t){in, sizeof(in)}, NULL, 0, &tail));
  TEST_ASSERT_EQUAL_UINT32(3, tail.len);
  TEST_ASSERT_EQUAL_PTR(in + 1, tail.data);
  err_h e = router_args_tail((cmd_in_t){in, 1}, NULL, 0, &tail);  /* key only: no tail byte */
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_LEN, e);
  err_free(e);
}

/* ---- router_call_op: a module's command table ------------------------------------------------------------------- */

static void test_op_runs_the_side_the_direction_asks_for(void) {
  uint8_t buf[16];
  cmd_out_t out = make_out(buf, sizeof(buf));
  int ctx = 0;
  const uint8_t write[] = {CMD_DIR_WRITE, CODE_PAIR, 0x07, 0x34, 0x12};  /* [dir][code][args] */
  TEST_ASSERT_NULL(router_call_op(k_ops, ARRAY_LEN(k_ops), &ctx, (cmd_in_t){write, sizeof(write)}, &out));
  TEST_ASSERT_EQUAL_INT(1, g.calls);
  TEST_ASSERT_EQUAL_PTR(&ctx, g.ctx);  /* the registry's ctx goes through */
  TEST_ASSERT_EQUAL_UINT8(7, g.a);
  TEST_ASSERT_EQUAL_HEX16(0x1234, g.b);
  const uint8_t read[] = {CMD_DIR_READ, CODE_PING};
  out.len = 0;
  TEST_ASSERT_NULL(router_call_op(k_ops, ARRAY_LEN(k_ops), NULL, (cmd_in_t){read, sizeof(read)}, &out));
  TEST_ASSERT_EQUAL_UINT32(1, out.len);
  TEST_ASSERT_EQUAL_HEX8(0x55, buf[0]);
}

static void test_op_the_handler_gets_code_and_args_so_the_macros_parse_it(void) {
  uint8_t buf[16];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t req[] = {CMD_DIR_WRITE, CODE_NAME, 0x09, 'x', 'y'};
  TEST_ASSERT_NULL(router_call_op(k_ops, ARRAY_LEN(k_ops), NULL, (cmd_in_t){req, sizeof(req)}, &out));
  TEST_ASSERT_EQUAL_UINT8(9, g.a);
  TEST_ASSERT_EQUAL_UINT8(2, g.tail_len);
  TEST_ASSERT_EQUAL_UINT8('x', g.tail0);
  TEST_ASSERT_EQUAL_UINT32(2, out.len);
}

static void test_op_a_request_too_short_for_dir_and_code(void) {
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t one[] = {CMD_DIR_READ};
  err_h e = router_call_op(k_ops, ARRAY_LEN(k_ops), NULL, (cmd_in_t){one, 1}, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_NO_KEY, e);
  err_free(e);
  e = router_call_op(k_ops, ARRAY_LEN(k_ops), NULL, (cmd_in_t){NULL, 0}, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_NO_KEY, e);
  err_free(e);
}

static void test_op_unknown_code(void) {
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t req[] = {CMD_DIR_READ, 0x99};
  err_h e = router_call_op(k_ops, ARRAY_LEN(k_ops), NULL, (cmd_in_t){req, sizeof(req)}, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_UNKNOWN_OP, e);
  TEST_ASSERT_EQUAL_UINT8(0x99, ERR_DATA(e, ERR_ROUTER_UNKNOWN_OP)->code);
  TEST_ASSERT_NULL(e->next);  /* not wrapped: there was no op to name */
  err_free(e);
  TEST_ASSERT_EQUAL_INT(0, g.calls);
}

static void test_op_a_side_the_command_does_not_have(void) {
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t write_to_readonly[] = {CMD_DIR_WRITE, CODE_READONLY};
  err_h e = router_call_op(k_ops, ARRAY_LEN(k_ops), NULL, (cmd_in_t){write_to_readonly, 2}, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_OP, e);  /* the route step wraps the cause */
  TEST_ASSERT_EQUAL_UINT8(CODE_READONLY, ERR_DATA(e, ERR_ROUTER_OP)->code);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_DIRECTION, e->next->tag);
  TEST_ASSERT_EQUAL_UINT8(CMD_DIR_WRITE, ERR_DATA(e->next, ERR_CMD_DIRECTION)->dir);
  err_free(e);
  const uint8_t read_of_writeonly[] = {CMD_DIR_READ, CODE_WRITEONLY};
  e = router_call_op(k_ops, ARRAY_LEN(k_ops), NULL, (cmd_in_t){read_of_writeonly, 2}, &out);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_DIRECTION, e->next->tag);
  err_free(e);
  const uint8_t odd_dir[] = {0x05, CODE_PAIR};  /* both sides exist, the direction is neither */
  e = router_call_op(k_ops, ARRAY_LEN(k_ops), NULL, (cmd_in_t){odd_dir, 2}, &out);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_DIRECTION, e->next->tag);
  err_free(e);
  TEST_ASSERT_EQUAL_INT(0, g.calls);  /* no handler ran */
}

static void test_op_a_bad_payload_names_the_op_and_the_size(void) {
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  const uint8_t req[] = {CMD_DIR_WRITE, CODE_PAIR, 0x07};  /* b is missing */
  err_h e = router_call_op(k_ops, ARRAY_LEN(k_ops), NULL, (cmd_in_t){req, sizeof(req)}, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_OP, e);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_BAD_LEN, e->next->tag);
  TEST_ASSERT_EQUAL_UINT16(1, ERR_DATA(e->next, ERR_CMD_BAD_LEN)->got);
  TEST_ASSERT_EQUAL_UINT16(3, ERR_DATA(e->next, ERR_CMD_BAD_LEN)->need);
  TEST_ASSERT_NULL(e->next->next);
  TEST_ASSERT_EQUAL_INT(0, g.calls);  /* the body never ran */
  err_free(e);
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
  const uint8_t req[] = {KEY_MOD, CMD_DIR_WRITE, CODE_PAIR, 0x07, 0x34, 0x12};
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
  const uint8_t req[] = {KEY_MOD, CMD_DIR_WRITE, CODE_FAIL, 0x00};  /* id 0: the op raises BAD_ARG */
  err_h e = router_route((cmd_in_t){req, sizeof(req)}, &out);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_KEY, e);                    /* outermost: where it started */
  TEST_ASSERT_EQUAL_UINT8(KEY_MOD, ERR_DATA(e, ERR_ROUTER_KEY)->key);
  TEST_ASSERT_EQUAL_HEX32(ERR_ROUTER_OP, e->next->tag);
  TEST_ASSERT_EQUAL_UINT8(CODE_FAIL, ERR_DATA(e->next, ERR_ROUTER_OP)->code);
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
  const uint8_t req[] = {KEY_MOD, CMD_DIR_READ, CODE_PARTIAL};
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

/* ---- router_register ------------------------------------------------------------------------------------------ */

static void test_register_refuses_key_zero_and_no_handler(void) {
  err_h e = router_register(0, root_handler);
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_ARG, e);
  err_free(e);
  e = router_register(0x50, NULL);
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
  err_h e = router_register(KEY_MOD, root_handler);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_KEY_TAKEN, e);
  TEST_ASSERT_EQUAL_UINT8(KEY_MOD, ERR_DATA(e, ERR_ROUTER_KEY_TAKEN)->key);
  err_free(e);
  const uint8_t req[] = {KEY_MOD, CMD_DIR_READ, CODE_PING};  /* the first binding is kept */
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  TEST_ASSERT_NULL(router_route((cmd_in_t){req, sizeof(req)}, &out));
  TEST_ASSERT_EQUAL_HEX8(0x55, buf[0]);
}

static void test_register_a_full_table_refuses_one_more_and_keeps_the_rest(void) {
  uint8_t key = 0x40;
  err_h e = NULL;
  while (s_registered < CONFIG_ROUTER_MAX_KEYS) {
    TEST_ASSERT_NULL(router_register(key++, root_handler));
    s_registered++;
  }
  e = router_register(key, root_handler);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_TABLE_FULL, e);
  TEST_ASSERT_EQUAL_UINT8(key, ERR_DATA(e, ERR_ROUTER_TABLE_FULL)->key);
  err_free(e);
  const uint8_t req[] = {KEY_ROOT, 0x01};  /* what was bound still routes */
  uint8_t buf[4];
  cmd_out_t out = make_out(buf, sizeof(buf));
  TEST_ASSERT_NULL(router_route((cmd_in_t){req, sizeof(req)}, &out));
}

/* ---- router_report_cmd ---------------------------------------------------------------------------------------- */

static void test_report_cmd_logs_the_command_with_the_error(void) {
  const uint8_t req[] = {0x02, 0x02, 0x00};
  const cmd_req_t cmd = {.req = req, .req_len = sizeof(req), .seq = 7, .src = 0xF0, .flags = 0};
  router_report_cmd(&cmd, ERR_NEW(ERR_CMD_BAD_ARG));
  TEST_ASSERT_EQUAL_INT(2, host_log_count());
  TEST_ASSERT_EQUAL_STRING("cmd src f0 seq 7 flags 00 req 02 02 00", host_log_line(0));
  TEST_ASSERT_NOT_NULL(strstr(host_log_line(1), "ERR_CMD_BAD_ARG"));
}

static void test_report_cmd_shows_only_the_start_of_a_long_request(void) {
  uint8_t req[100];
  for (int i = 0; i < 100; i++) req[i] = (uint8_t)i;
  const cmd_req_t cmd = {.req = req, .req_len = sizeof(req), .seq = 1, .src = 1, .flags = 3};
  router_report_cmd(&cmd, ERR_NEW(ERR_CMD_BAD_ARG));
  const char* line = host_log_line(0);
  TEST_ASSERT_NOT_NULL(strstr(line, "flags 03 req 00 01 02"));
  TEST_ASSERT_NOT_NULL(strstr(line, " 1f ..."));  /* 32 bytes shown, then the cut mark */
  TEST_ASSERT_NULL(strstr(line, " 20"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_args_copies_exactly_the_payload_after_the_key);
  RUN_TEST(test_args_refuses_a_payload_of_the_wrong_size);
  RUN_TEST(test_args_of_size_zero_only_checks_that_nothing_follows);
  RUN_TEST(test_args_tail_splits_header_and_tail);
  RUN_TEST(test_args_tail_needs_at_least_one_tail_byte);
  RUN_TEST(test_args_tail_of_size_zero_is_all_tail);
  RUN_TEST(test_op_runs_the_side_the_direction_asks_for);
  RUN_TEST(test_op_the_handler_gets_code_and_args_so_the_macros_parse_it);
  RUN_TEST(test_op_a_request_too_short_for_dir_and_code);
  RUN_TEST(test_op_unknown_code);
  RUN_TEST(test_op_a_side_the_command_does_not_have);
  RUN_TEST(test_op_a_bad_payload_names_the_op_and_the_size);
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
  RUN_TEST(test_report_cmd_logs_the_command_with_the_error);
  RUN_TEST(test_report_cmd_shows_only_the_start_of_a_long_request);
  RUN_TEST(test_register_a_full_table_refuses_one_more_and_keeps_the_rest);  /* last: it fills the table */
  return UNITY_END();
}
