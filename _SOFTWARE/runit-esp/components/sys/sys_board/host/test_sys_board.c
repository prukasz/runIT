/* Host tests of sys_board (JSON router CRUD + sys_board_boot RUN_ALL injection). */
#include <string.h>

#include "host_assert.h"
#include "nvs_store.h"
#include "router.h"
#include "router_queue.h"
#include "sys_actions.h"
#include "sys_actions_errors.h"
#include "sys_actions_router.h"
#include "sys_board.h"
#include "sys_board_router.h"
#include "unity.h"

extern void host_nvs_store_reset(void);

ERR_MODULE_DEFINE(MODULE_ID_ACTIONS, ERR_SYS_ACTIONS_MAP)

/* ---- Stubs for sys_actions_info & router_inject ---- */
static bool s_has_board_record;
static int s_inject_calls;
static uint8_t s_injected[32];
static uint32_t s_injected_len;

err_h sys_actions_info(uint32_t id, sys_actions_info_t* info) {
  if (!s_has_board_record || id != SYS_ACTIONS_ID_BOARD) {
    return ERR_NEW(ERR_ACTION_NOT_FOUND, .id = id);
  }
  *info = (sys_actions_info_t){.bytes = 10, .count = 1, .crc32 = 0x11223344u};
  return NULL;
}

err_h router_inject(cmd_in_t req, uint8_t flags, router_done_f done, void* ctx) {
  (void)flags, (void)done, (void)ctx;
  s_inject_calls++;
  s_injected_len = req.len < sizeof(s_injected) ? req.len : sizeof(s_injected);
  memcpy(s_injected, req.data, s_injected_len);
  return NULL;
}

static err_h route_board(uint8_t dir, uint8_t op, const void* args, uint32_t args_len, uint8_t* reply,
                         uint32_t reply_cap, uint32_t* reply_len) {
  uint8_t req[128];
  req[0] = MODULE_ID_BOARD;
  req[1] = dir;
  req[2] = op;
  if (args_len > 0) {
    memcpy(req + 3, args, args_len);
  }
  cmd_out_t out = {.data = reply, .cap = reply_cap, .len = 0};
  err_h e = router_route((cmd_in_t){req, 3u + args_len}, &out);
  if (reply_len != NULL) {
    *reply_len = out.len;
  }
  return e;
}

void setUp(void) {
  host_nvs_store_reset();
  s_has_board_record = false;
  s_inject_calls = 0;
  s_injected_len = 0;
  TEST_ASSERT_NULL(sys_board_init());
}

void tearDown(void) {}

static void test_boot_skips_when_no_profile_and_injects_run_all_when_present(void) {
  /* No profile saved -> returns NULL without injecting */
  TEST_ASSERT_NULL(sys_board_boot());
  TEST_ASSERT_EQUAL_INT(0, s_inject_calls);

  /* Profile present -> injects [MODULE_ID_ACTIONS, WRITE, ACTIONS_OP_RUN_ALL, SYS_ACTIONS_ID_BOARD] */
  s_has_board_record = true;
  TEST_ASSERT_NULL(sys_board_boot());
  TEST_ASSERT_EQUAL_INT(1, s_inject_calls);
  TEST_ASSERT_EQUAL_UINT32(7, s_injected_len);
  TEST_ASSERT_EQUAL_UINT8(MODULE_ID_ACTIONS, s_injected[0]);
  TEST_ASSERT_EQUAL_UINT8(CMD_DIR_WRITE, s_injected[1]);
  TEST_ASSERT_EQUAL_UINT8(ACTIONS_OP_RUN_ALL, s_injected[2]);
  uint32_t id = 0;
  memcpy(&id, s_injected + 3, sizeof(id));
  TEST_ASSERT_EQUAL_HEX32(SYS_ACTIONS_ID_BOARD, id);
}

static void test_router_json_append_info_read_and_erase(void) {
  uint8_t reply[64];
  uint32_t rlen = 0;

  /* Append 2 chunks: "{\"a\":" + "1}" */
  uint8_t p1[4 + 5] = {0};
  memcpy(p1 + 4, "{\"a\":", 5);
  TEST_ASSERT_NULL(route_board(CMD_DIR_WRITE, BOARD_OP_JSON_APPEND, p1, sizeof(p1), reply, sizeof(reply), &rlen));

  uint8_t p2[4 + 2] = {5, 0, 0, 0, '1', '}'};
  TEST_ASSERT_NULL(route_board(CMD_DIR_WRITE, BOARD_OP_JSON_APPEND, p2, sizeof(p2), reply, sizeof(reply), &rlen));

  /* INFO */
  TEST_ASSERT_NULL(route_board(CMD_DIR_READ, BOARD_OP_JSON_INFO, NULL, 0, reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_UINT32(sizeof(nvs_store_json_info_t), rlen);
  nvs_store_json_info_t info;
  memcpy(&info, reply, sizeof(info));
  TEST_ASSERT_EQUAL_UINT32(7, info.bytes);
  TEST_ASSERT_EQUAL_UINT16(2, info.chunks);

  /* READ chunk 0 & 1 */
  sys_board_read_args_t rd = {.chunk = 0};
  TEST_ASSERT_NULL(route_board(CMD_DIR_READ, BOARD_OP_JSON_READ, &rd, sizeof(rd), reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_UINT32(5, rlen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY("{\"a\":", reply, 5);

  rd.chunk = 1;
  TEST_ASSERT_NULL(route_board(CMD_DIR_READ, BOARD_OP_JSON_READ, &rd, sizeof(rd), reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_UINT32(2, rlen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY("1}", reply, 2);

  /* ERASE resets to empty */
  TEST_ASSERT_NULL(route_board(CMD_DIR_WRITE, BOARD_OP_JSON_ERASE, NULL, 0, reply, sizeof(reply), &rlen));
  TEST_ASSERT_NULL(route_board(CMD_DIR_READ, BOARD_OP_JSON_INFO, NULL, 0, reply, sizeof(reply), &rlen));
  memcpy(&info, reply, sizeof(info));
  TEST_ASSERT_EQUAL_UINT32(0, info.bytes);
  TEST_ASSERT_EQUAL_UINT16(0, info.chunks);
}

int main(void) {
  TEST_ASSERT_NULL(router_root_init());
  UNITY_BEGIN();
  RUN_TEST(test_boot_skips_when_no_profile_and_injects_run_all_when_present);
  RUN_TEST(test_router_json_append_info_read_and_erase);
  return UNITY_END();
}
