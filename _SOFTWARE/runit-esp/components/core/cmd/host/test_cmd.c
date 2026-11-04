/* Host tests of the cmd component: the real cmd.c and errors; only the platform is replaced.
 * Run: python test_host/run.py cmd   (runit-esp/test_host/HOST_TESTS.MD) */
#include <string.h>
#include "unity.h"
#include "host_assert.h"
#include "cmd.h"
#include "err_internal.h"  /* the registry lookup (err_module, err_tag_info): only the wire-size test needs it */
#include "host_mocks.h"

void setUp(void) {
  host_log_clear();
  host_time_set(0);
}
void tearDown(void) {}

/* ---- handlers used as the router and the device registry use them --------------------------------------------- */

static err_h h_read(void* ctx, cmd_in_t in, cmd_out_t* out) {
  (void)in;
  const uint8_t value = *(const uint8_t*)ctx;
  return cmd_reply(out, &value, sizeof(value));
}
static err_h h_write(void* ctx, cmd_in_t in, cmd_out_t* out) {
  (void)out;
  ERR_CHECK(in.len == 1, ERR_CMD_BAD_LEN, .got = (uint16_t)in.len, .need = 1);
  *(uint8_t*)ctx = in.data[0];
  return NULL;
}

/* ---- cmd_side ------------------------------------------------------------------------------------------------- */

static void test_side_picks_the_handler_of_the_direction(void) {
  cmd_f fn = NULL;
  TEST_ASSERT_NULL(cmd_side(h_read, h_write, CMD_DIR_READ, &fn));
  TEST_ASSERT_TRUE(fn == h_read);
  TEST_ASSERT_NULL(cmd_side(h_read, h_write, CMD_DIR_WRITE, &fn));
  TEST_ASSERT_TRUE(fn == h_write);
}

static void test_side_a_command_with_one_side_refuses_the_other(void) {
  cmd_f fn = h_read;
  err_h e = cmd_side(h_read, NULL, CMD_DIR_WRITE, &fn);  /* a read-only command asked to write */
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_ERR_TAG(ERR_CMD_DIRECTION, e);
  TEST_ASSERT_EQUAL_UINT8(CMD_DIR_WRITE, ERR_DATA(e, ERR_CMD_DIRECTION)->dir);
  TEST_ASSERT_NULL(fn);
  err_free(e);

  e = cmd_side(NULL, h_write, CMD_DIR_READ, &fn);        /* and a write-only one asked to read */
  TEST_ASSERT_ERR_TAG(ERR_CMD_DIRECTION, e);
  err_free(e);
}

static void test_side_a_dir_that_is_neither_read_nor_write(void) {
  cmd_f fn = h_read;
  const uint8_t bad[] = {2, 0x7F, 0xFF};
  for (unsigned i = 0; i < sizeof(bad); i++) {
    err_h e = cmd_side(h_read, h_write, bad[i], &fn);    /* both sides exist: still refused */
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_ERR_TAG(ERR_CMD_DIRECTION, e);
    TEST_ASSERT_EQUAL_UINT8(bad[i], ERR_DATA(e, ERR_CMD_DIRECTION)->dir);
    TEST_ASSERT_NULL(fn);
    err_free(e);
    fn = h_read;
  }
}

static void test_a_command_is_run_through_its_side(void) {
  uint8_t state = 41;
  uint8_t buf[8];
  cmd_out_t out = {.data = buf, .cap = sizeof(buf), .len = 0};
  cmd_f fn;

  TEST_ASSERT_NULL(cmd_side(h_read, h_write, CMD_DIR_READ, &fn));
  TEST_ASSERT_NULL(fn(&state, (cmd_in_t){NULL, 0}, &out));
  TEST_ASSERT_EQUAL_UINT32(1, out.len);
  TEST_ASSERT_EQUAL_UINT8(41, buf[0]);

  const uint8_t arg = 42;
  TEST_ASSERT_NULL(cmd_side(h_read, h_write, CMD_DIR_WRITE, &fn));
  out.len = 0;
  TEST_ASSERT_NULL(fn(&state, (cmd_in_t){&arg, 1}, &out));
  TEST_ASSERT_EQUAL_UINT32(0, out.len);  /* a write answers no data */
  TEST_ASSERT_EQUAL_UINT8(42, state);

  err_h e = fn(&state, (cmd_in_t){&arg, 2}, &out);  /* the payload size is the handler's to check */
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_LEN, e);
  TEST_ASSERT_EQUAL_UINT16(2, ERR_DATA(e, ERR_CMD_BAD_LEN)->got);
  TEST_ASSERT_EQUAL_UINT16(1, ERR_DATA(e, ERR_CMD_BAD_LEN)->need);
  TEST_ASSERT_EQUAL_UINT8(42, state);               /* nothing was changed */
  err_free(e);
}

/* ---- cmd_reply ------------------------------------------------------------------------------------------------ */

static void test_reply_writes_the_whole_answer(void) {
  uint8_t buf[4] = {0};
  cmd_out_t out = {.data = buf, .cap = sizeof(buf), .len = 0};
  const uint8_t data[] = {1, 2, 3};
  TEST_ASSERT_NULL(cmd_reply(&out, data, sizeof(data)));
  TEST_ASSERT_EQUAL_UINT32(3, out.len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(data, buf, 3);
}

static void test_reply_fills_exactly_the_cap(void) {
  uint8_t buf[3];
  cmd_out_t out = {.data = buf, .cap = sizeof(buf), .len = 0};
  const uint8_t data[] = {9, 8, 7};
  TEST_ASSERT_NULL(cmd_reply(&out, data, sizeof(data)));
  TEST_ASSERT_EQUAL_UINT32(3, out.len);
}

static void test_reply_replaces_what_was_there(void) {
  uint8_t buf[8];
  cmd_out_t out = {.data = buf, .cap = sizeof(buf), .len = 0};
  const uint8_t first[] = {1, 2, 3, 4, 5};
  const uint8_t second[] = {9, 9};
  TEST_ASSERT_NULL(cmd_reply(&out, first, sizeof(first)));
  TEST_ASSERT_NULL(cmd_reply(&out, second, sizeof(second)));
  TEST_ASSERT_EQUAL_UINT32(2, out.len);  /* not 7: a reply is the whole answer */
  TEST_ASSERT_EQUAL_HEX8_ARRAY(second, buf, 2);
}

static void test_reply_too_long_is_refused_and_writes_nothing(void) {
  uint8_t buf[4];
  memset(buf, 0xEE, sizeof(buf));
  cmd_out_t out = {.data = buf, .cap = sizeof(buf), .len = 0};
  const uint8_t data[5] = {1, 2, 3, 4, 5};
  err_h e = cmd_reply(&out, data, sizeof(data));
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_ERR_TAG(ERR_CMD_OUT_FULL, e);
  TEST_ASSERT_EQUAL_UINT16(5, ERR_DATA(e, ERR_CMD_OUT_FULL)->need);
  TEST_ASSERT_EQUAL_UINT16(4, ERR_DATA(e, ERR_CMD_OUT_FULL)->cap);
  TEST_ASSERT_EQUAL_UINT32(0, out.len);
  const uint8_t untouched[] = {0xEE, 0xEE, 0xEE, 0xEE};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(untouched, buf, sizeof(buf));
  err_free(e);
}

static void test_reply_empty_answer(void) {
  uint8_t buf[4];
  cmd_out_t out = {.data = buf, .cap = sizeof(buf), .len = 3};
  uint8_t none = 0;
  TEST_ASSERT_NULL(cmd_reply(&out, &none, 0));
  TEST_ASSERT_EQUAL_UINT32(0, out.len);  /* an empty answer clears the earlier one */
}

/* ---- cmd_append ----------------------------------------------------------------------------------------------- */

static void test_append_builds_an_answer_in_pieces(void) {
  uint8_t buf[8] = {0};
  cmd_out_t out = {.data = buf, .cap = sizeof(buf), .len = 0};
  const uint8_t head[] = {0xA0, 0xA1};
  const uint8_t name[] = {'x', 'y', 'z'};
  TEST_ASSERT_NULL(cmd_append(&out, head, sizeof(head)));
  TEST_ASSERT_NULL(cmd_append(&out, name, sizeof(name)));
  TEST_ASSERT_EQUAL_UINT32(5, out.len);
  const uint8_t want[] = {0xA0, 0xA1, 'x', 'y', 'z'};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, buf, sizeof(want));
}

static void test_append_fills_exactly_the_cap(void) {
  uint8_t buf[4];
  cmd_out_t out = {.data = buf, .cap = sizeof(buf), .len = 0};
  const uint8_t two[] = {1, 2};
  TEST_ASSERT_NULL(cmd_append(&out, two, 2));
  TEST_ASSERT_NULL(cmd_append(&out, two, 2));
  TEST_ASSERT_EQUAL_UINT32(4, out.len);
}

static void test_append_past_the_cap_is_refused_and_keeps_the_answer(void) {
  uint8_t buf[4] = {0};
  cmd_out_t out = {.data = buf, .cap = sizeof(buf), .len = 0};
  const uint8_t three[] = {1, 2, 3};
  TEST_ASSERT_NULL(cmd_append(&out, three, 3));
  err_h e = cmd_append(&out, three, 3);                    /* 3 + 3 > 4 */
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_ERR_TAG(ERR_CMD_OUT_FULL, e);
  TEST_ASSERT_EQUAL_UINT16(6, ERR_DATA(e, ERR_CMD_OUT_FULL)->need);  /* what the whole answer would be */
  TEST_ASSERT_EQUAL_UINT16(4, ERR_DATA(e, ERR_CMD_OUT_FULL)->cap);
  TEST_ASSERT_EQUAL_UINT32(3, out.len);                    /* the first piece stays */
  TEST_ASSERT_EQUAL_HEX8_ARRAY(three, buf, 3);
  err_free(e);
}

/* ---- cmd_err_record ------------------------------------------------------------------------------------------- */

static void test_err_record_of_no_command_is_an_internal_record(void) {
  host_time_set(0x1122334455667788ull);
  err_h e = ERR_NEW(ERR_CMD_BAD_ARG);
  uint8_t a[64], b[64];
  uint32_t na = cmd_err_record(NULL, e, a, sizeof(a));
  uint32_t nb = err_record(e, b, sizeof(b));
  TEST_ASSERT_EQUAL_UINT32(nb, na);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(b, a, nb);
  TEST_ASSERT_EQUAL_HEX8(ERR_RECORD_SRC_INTERNAL, a[0]);
  err_free(e);
}

static void test_err_record_of_a_command_carries_its_origin_and_request(void) {
  const uint8_t req[] = {0x0B, CMD_DIR_WRITE, 0x03, 0x00, 0x42};
  const cmd_req_t cmd = {.req = req, .req_len = sizeof(req), .seq = 0xBEEF, .src = 0x07, .flags = 0x03};
  err_h e = ERR_NEW(ERR_CMD_BAD_LEN, .got = 2, .need = 1);
  uint8_t buf[64];
  uint32_t n = cmd_err_record(&cmd, e, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_HEX8(ERR_RECORD_SRC_COMMAND, buf[0]);
  const uint8_t origin[] = {0x07, 0xEF, 0xBE, 0x03, sizeof(req)};  /* from, seq LE, flags, request length */
  TEST_ASSERT_EQUAL_HEX8_ARRAY(origin, buf + 1 + ERR_RECORD_STAMP_BYTES, sizeof(origin));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(req, buf + 1 + ERR_RECORD_STAMP_BYTES + sizeof(origin), sizeof(req));
  uint8_t direct[64];
  TEST_ASSERT_EQUAL_UINT32(err_record_cmd(0x07, 0xBEEF, 0x03, req, sizeof(req), e, direct, sizeof(direct)), n);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(direct, buf, n);
  err_free(e);
}

static void test_err_record_of_a_command_without_a_request(void) {
  const cmd_req_t cmd = {.req = NULL, .req_len = 0, .seq = 1, .src = 0xFE, .flags = 0};
  err_h e = ERR_NEW(ERR_CMD_BAD_ARG);
  uint8_t buf[64];
  uint32_t n = cmd_err_record(&cmd, e, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_HEX8(0, buf[1 + ERR_RECORD_STAMP_BYTES + 4]);  /* request length 0 */
  err_free(e);
}

/* ---- the module's errors are on the wire: tags and sizes do not change ------------------------------------------ */

static void test_error_tags_are_registered_with_their_wire_sizes(void) {
  TEST_ASSERT_EQUAL_STRING("cmd", err_module(ERR_MOD_CMD)->name);
  TEST_ASSERT_EQUAL_HEX32(0x000B0001, ERR_CMD_BAD_LEN);
  TEST_ASSERT_EQUAL_HEX32(0x000B0002, ERR_CMD_OUT_FULL);
  TEST_ASSERT_EQUAL_HEX32(0x000B0003, ERR_CMD_BAD_ARG);
  TEST_ASSERT_EQUAL_HEX32(0x000B0004, ERR_CMD_DIRECTION);
  TEST_ASSERT_EQUAL_UINT8(4, err_tag_info(ERR_CMD_BAD_LEN)->size);
  TEST_ASSERT_EQUAL_UINT8(4, err_tag_info(ERR_CMD_OUT_FULL)->size);
  TEST_ASSERT_EQUAL_UINT8(0, err_tag_info(ERR_CMD_BAD_ARG)->size);
  TEST_ASSERT_EQUAL_UINT8(1, err_tag_info(ERR_CMD_DIRECTION)->size);
  TEST_ASSERT_EQUAL_UINT8(ERR_SEV_HIGH, err_tag_info(ERR_CMD_BAD_LEN)->sev);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_side_picks_the_handler_of_the_direction);
  RUN_TEST(test_side_a_command_with_one_side_refuses_the_other);
  RUN_TEST(test_side_a_dir_that_is_neither_read_nor_write);
  RUN_TEST(test_a_command_is_run_through_its_side);
  RUN_TEST(test_reply_writes_the_whole_answer);
  RUN_TEST(test_reply_fills_exactly_the_cap);
  RUN_TEST(test_reply_replaces_what_was_there);
  RUN_TEST(test_reply_too_long_is_refused_and_writes_nothing);
  RUN_TEST(test_reply_empty_answer);
  RUN_TEST(test_append_builds_an_answer_in_pieces);
  RUN_TEST(test_append_fills_exactly_the_cap);
  RUN_TEST(test_append_past_the_cap_is_refused_and_keeps_the_answer);
  RUN_TEST(test_err_record_of_no_command_is_an_internal_record);
  RUN_TEST(test_err_record_of_a_command_carries_its_origin_and_request);
  RUN_TEST(test_err_record_of_a_command_without_a_request);
  RUN_TEST(test_error_tags_are_registered_with_their_wire_sizes);
  return UNITY_END();
}
