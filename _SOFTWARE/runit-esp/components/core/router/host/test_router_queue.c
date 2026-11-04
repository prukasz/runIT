/* Host tests of the router's queue: what router_submit / router_inject accept and refuse, how the two queues fill,
 * and router_run_urgent's one waiting slot. The tasks are NOT started here (no router_init), so nothing is taken out
 * of the queues: they only fill. What the tasks do with a command is in test_router_tasks.c.
 * Run: python test_host/run.py router   (runit-esp/test_host/HOST_TESTS.MD) */
#include <string.h>
#include "unity.h"
#include "host_assert.h"
#include "router.h"
#include "host_mocks.h"

void setUp(void) {
  host_log_clear();
}
void tearDown(void) {}

static cmd_req_t make_cmd(const uint8_t* req, uint32_t len, uint8_t flags) {
  return (cmd_req_t){.req = req, .req_len = len, .seq = 1, .src = 0x07, .flags = flags};
}

/* ---- router_submit: what is refused --------------------------------------------------------------------------- */

static void test_submit_needs_a_command_with_a_request(void) {
  err_h e = router_submit(NULL, NULL, NULL);
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_ARG, e);
  err_free(e);
  cmd_req_t no_req = make_cmd(NULL, 5, 0);
  e = router_submit(&no_req, NULL, NULL);
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_ARG, e);
  err_free(e);
}

static void test_submit_refuses_a_request_outside_2_to_the_frame_data(void) {
  uint8_t req[CMD_DATA_MAX + 1] = {0};
  const uint32_t bad[] = {0, 1, CMD_DATA_MAX + 1};
  for (unsigned i = 0; i < ARRAY_LEN(bad); i++) {
    cmd_req_t cmd = make_cmd(req, bad[i], 0);
    err_h e = router_submit(&cmd, NULL, NULL);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_ERR_TAG(ERR_ROUTER_FRAME_LEN, e);
    TEST_ASSERT_EQUAL_UINT8(0x07, ERR_DATA(e, ERR_ROUTER_FRAME_LEN)->src);
    TEST_ASSERT_EQUAL_UINT16(bad[i], ERR_DATA(e, ERR_ROUTER_FRAME_LEN)->len);
    TEST_ASSERT_EQUAL_UINT16(CMD_DATA_MAX, ERR_DATA(e, ERR_ROUTER_FRAME_LEN)->max);
    err_free(e);
  }
}

static void test_submit_accepts_the_smallest_and_the_largest_request(void) {
  uint8_t req[CMD_DATA_MAX] = {0};
  cmd_req_t small = make_cmd(req, 2, 0);
  cmd_req_t large = make_cmd(req, CMD_DATA_MAX, ROUTER_FLAG_URGENT);
  TEST_ASSERT_NULL(router_submit(&small, NULL, NULL));
  TEST_ASSERT_NULL(router_submit(&large, NULL, NULL));
}

/* ---- router_inject -------------------------------------------------------------------------------------------- */

static void test_inject_needs_a_request(void) {
  err_h e = router_inject((cmd_in_t){NULL, 3}, 0, NULL, NULL);
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_ARG, e);
  err_free(e);
  const uint8_t req[] = {1, 0};
  e = router_inject((cmd_in_t){req, 0}, 0, NULL, NULL);
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_ARG, e);
  err_free(e);
}

static void test_inject_goes_through_submit_and_its_limits(void) {
  const uint8_t one[] = {1};
  err_h e = router_inject((cmd_in_t){one, 1}, 0, NULL, NULL);  /* a self command too short: [key][dir] */
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_FRAME_LEN, e);
  TEST_ASSERT_EQUAL_UINT8(ROUTER_SRC_SELF, ERR_DATA(e, ERR_ROUTER_FRAME_LEN)->src);
  err_free(e);
  const uint8_t ok[] = {1, 0};
  TEST_ASSERT_NULL(router_inject((cmd_in_t){ok, 2}, ROUTER_FLAG_NOREPLY, NULL, NULL));
}

/* ---- router_run_urgent: one fn waits ------------------------------------------------------------------------- */

static void fn_a(void) {}
static void fn_b(void) {}

static void test_run_urgent_has_one_waiting_slot(void) {
  err_h e = router_run_urgent(NULL);
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_ARG, e);
  err_free(e);
  TEST_ASSERT_NULL(router_run_urgent(fn_a));
  TEST_ASSERT_NULL(router_run_urgent(fn_a));  /* the same fn again: it still runs once */
  e = router_run_urgent(fn_b);                /* another fn while one waits */
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_BUSY, e);
  err_free(e);
  TEST_ASSERT_NULL(router_run_urgent(fn_a));  /* the refusal did not displace the waiting one */
}

/* ---- where the caller is -------------------------------------------------------------------------------------- */

static void test_a_caller_outside_the_router_tasks(void) {
  TEST_ASSERT_FALSE(router_is_task());
  TEST_ASSERT_NULL(router_origin());
}

/* ---- the queues fill, each its own ---------------------------------------------------------------------------- */

static int fill(uint8_t flags, err_h* refused) {
  const uint8_t req[] = {1, 0};
  cmd_req_t cmd = make_cmd(req, sizeof(req), flags);
  int accepted = 0;
  err_h e = NULL;
  while (accepted < 100000 && (e = router_submit(&cmd, NULL, NULL)) == NULL) {
    accepted++;
  }
  *refused = e;
  return accepted;
}

static void test_a_full_work_queue_refuses_and_the_urgent_one_is_still_open(void) {
  err_h refused = NULL;
  const int work = fill(0, &refused);
  TEST_ASSERT_TRUE(work > 10 && work < 100000);  /* it filled: a bounded queue, not an endless one */
  TEST_ASSERT_NOT_NULL(refused);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_QUEUE_FULL, refused);
  TEST_ASSERT_EQUAL_UINT8(0x07, ERR_DATA(refused, ERR_ROUTER_QUEUE_FULL)->src);
  TEST_ASSERT_EQUAL_UINT8(0, ERR_DATA(refused, ERR_ROUTER_QUEUE_FULL)->urgent);
  err_free(refused);

  const uint8_t req[] = {1, 0};
  cmd_req_t urgent = make_cmd(req, sizeof(req), ROUTER_FLAG_URGENT);
  TEST_ASSERT_NULL(router_submit(&urgent, NULL, NULL));  /* the work queue being full does not touch it */
}

static void test_a_full_urgent_queue_refuses_and_says_urgent(void) {
  err_h refused = NULL;
  const int urgent = fill(ROUTER_FLAG_URGENT, &refused);
  TEST_ASSERT_TRUE(urgent > 5 && urgent < 100000);
  TEST_ASSERT_ERR_TAG(ERR_ROUTER_QUEUE_FULL, refused);
  TEST_ASSERT_EQUAL_UINT8(1, ERR_DATA(refused, ERR_ROUTER_QUEUE_FULL)->urgent);
  TEST_ASSERT_EQUAL_UINT8(ERR_SEV_MEDIUM, err_sev(refused));
  err_free(refused);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_submit_needs_a_command_with_a_request);
  RUN_TEST(test_submit_refuses_a_request_outside_2_to_the_frame_data);
  RUN_TEST(test_submit_accepts_the_smallest_and_the_largest_request);
  RUN_TEST(test_inject_needs_a_request);
  RUN_TEST(test_inject_goes_through_submit_and_its_limits);
  RUN_TEST(test_run_urgent_has_one_waiting_slot);
  RUN_TEST(test_a_caller_outside_the_router_tasks);
  RUN_TEST(test_a_full_work_queue_refuses_and_the_urgent_one_is_still_open);  /* the queues fill and stay full: last */
  RUN_TEST(test_a_full_urgent_queue_refuses_and_says_urgent);
  return UNITY_END();
}
