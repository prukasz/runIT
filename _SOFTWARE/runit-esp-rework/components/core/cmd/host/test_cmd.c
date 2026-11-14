/* Host tests of the cmd component: inline cmd_reply / cmd_append and cmd error tags.
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

/* ---- the module's errors are on the wire: tags and sizes do not change ------------------------------------------ */

static void test_error_tags_are_registered_with_their_wire_sizes(void) {
  TEST_ASSERT_EQUAL_STRING("cmd", err_module(ERR_MOD_CMD)->name);
  TEST_ASSERT_EQUAL_HEX32(0x00270001, ERR_CMD_BAD_LEN);
  TEST_ASSERT_EQUAL_HEX32(0x00270002, ERR_CMD_OUT_FULL);
  TEST_ASSERT_EQUAL_HEX32(0x00270003, ERR_CMD_BAD_ARG);
  TEST_ASSERT_EQUAL_HEX32(0x00270004, ERR_CMD_DIRECTION);
  TEST_ASSERT_EQUAL_UINT8(4, err_tag_info(ERR_CMD_BAD_LEN)->size);
  TEST_ASSERT_EQUAL_UINT8(4, err_tag_info(ERR_CMD_OUT_FULL)->size);
  TEST_ASSERT_EQUAL_UINT8(0, err_tag_info(ERR_CMD_BAD_ARG)->size);
  TEST_ASSERT_EQUAL_UINT8(1, err_tag_info(ERR_CMD_DIRECTION)->size);
  TEST_ASSERT_EQUAL_UINT8(ERR_SEV_HIGH, err_tag_info(ERR_CMD_BAD_LEN)->sev);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_reply_writes_the_whole_answer);
  RUN_TEST(test_reply_fills_exactly_the_cap);
  RUN_TEST(test_reply_replaces_what_was_there);
  RUN_TEST(test_reply_too_long_is_refused_and_writes_nothing);
  RUN_TEST(test_reply_empty_answer);
  RUN_TEST(test_append_builds_an_answer_in_pieces);
  RUN_TEST(test_append_fills_exactly_the_cap);
  RUN_TEST(test_append_past_the_cap_is_refused_and_keeps_the_answer);
  RUN_TEST(test_error_tags_are_registered_with_their_wire_sizes);
  return UNITY_END();
}
