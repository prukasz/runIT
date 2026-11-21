/* Host tests of core/board_id (default ID, NVS load/save, range validation, corrupt value fallback). */
#include "board_id.h"
#include "board_id_port.h"
#include "cmd.h"
#include "host_assert.h"
#include "nvs_store.h"
#include "sdkconfig.h"
#include "unity.h"

extern void host_nvs_store_reset(void);

static uint32_t s_rand = 0x12345678u;
static uint32_t s_build = 0xABCDEF01u;
uint32_t board_id_port_random(void) { return s_rand; }
uint32_t board_id_port_build(void) { return s_build; }

void setUp(void) {
  host_nvs_store_reset();
}

void tearDown(void) {}

static void test_default_id_and_boot_build_ids(void) {
  TEST_ASSERT_NULL(board_id_init());
  TEST_ASSERT_EQUAL_UINT8(CONFIG_BOARD_ID_DEFAULT, board_id());
  TEST_ASSERT_EQUAL_HEX32(s_rand, board_boot_id());
  TEST_ASSERT_EQUAL_HEX32(s_build, board_build_id());
}

static void test_save_reload_and_validation(void) {
  TEST_ASSERT_NULL(board_id_init());

  /* Client & reserved IDs refused (0xF0..0xFF) */
  err_h e = board_id_save(CMD_SRC_CLIENT_FIRST);
  TEST_ASSERT_ERR_TAG(ERR_BAD_ARG, e);
  err_free(e);

  e = board_id_save(CMD_SRC_SELF);
  TEST_ASSERT_ERR_TAG(ERR_BAD_ARG, e);
  err_free(e);

  e = board_id_save(CMD_SRC_ALL);
  TEST_ASSERT_ERR_TAG(ERR_BAD_ARG, e);
  err_free(e);

  /* Valid board ID persisted and loaded on next init */
  TEST_ASSERT_NULL(board_id_save(0x42u));
  TEST_ASSERT_EQUAL_UINT8(CONFIG_BOARD_ID_DEFAULT, board_id());  /* takes effect next boot */
  TEST_ASSERT_NULL(board_id_init());
  TEST_ASSERT_EQUAL_UINT8(0x42u, board_id());

  /* CMD_SRC_NONE (0) is also valid */
  TEST_ASSERT_NULL(board_id_save(CMD_SRC_NONE));
  TEST_ASSERT_NULL(board_id_init());
  TEST_ASSERT_EQUAL_UINT8(CMD_SRC_NONE, board_id());
}

static void test_corrupt_stored_id_falls_back_to_default(void) {
  nvs_store_ns_t ns = 0;
  TEST_ASSERT_NULL(nvs_store_open("nvs", "board_id", &ns));
  const uint8_t bad = 0xF5u;
  TEST_ASSERT_NULL(nvs_store_set(ns, "id", &bad, sizeof(bad)));

  TEST_ASSERT_NULL(board_id_init());
  TEST_ASSERT_EQUAL_UINT8(CONFIG_BOARD_ID_DEFAULT, board_id());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_default_id_and_boot_build_ids);
  RUN_TEST(test_save_reload_and_validation);
  RUN_TEST(test_corrupt_stored_id_falls_back_to_default);
  return UNITY_END();
}
