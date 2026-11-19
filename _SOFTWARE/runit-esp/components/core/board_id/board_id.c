/**
 * @file board_id.c
 * @brief The board's identity (board_id.h), with no implementation in it: the id through the store, the boot id and the
 * build from this chip's part (internal/board_id_port.h, esp/).
 */

#include "board_id.h"

#include <stdbool.h>

#include "board_id_port.h"
#include "cmd.h"
#include "nvs_store.h"
#include "sdkconfig.h"

#define PARTITION "nvs"
#define NAMESPACE "board_id"
#define KEY_ID    "id"

static struct {
  uint8_t id;
  uint32_t boot_id;
  uint32_t build_id;
} s_board;

static bool id_ok(uint8_t id) {
  return id == CMD_SRC_NONE || (id >= CMD_SRC_BOARD_FIRST && id <= CMD_SRC_BOARD_LAST);
}

err_h board_id_init(void) {
  s_board.id = CONFIG_BOARD_ID_DEFAULT;
  s_board.boot_id = board_id_port_random();
  s_board.build_id = board_id_port_build();
  nvs_store_ns_t ns = 0;
  ERR_TRY(nvs_store_open(PARTITION, NAMESPACE, &ns));
  uint8_t id = 0;
  uint32_t len = sizeof(id);
  err_h err = nvs_store_get(ns, KEY_ID, &id, &len);
  if (err != NULL && err_tag(err) == ERR_NVS_NOT_FOUND) {
    err_free(err);
    return NULL;  // none kept: the default
  }
  ERR_TRY(err);
  s_board.id = len == sizeof(id) && id_ok(id) ? id : s_board.id;
  return NULL;
}

uint8_t board_id(void) {
  return s_board.id;
}

err_h board_id_save(uint8_t id) {
  ERR_CHECK(id_ok(id), ERR_CMD_BAD_ARG);
  nvs_store_ns_t ns = 0;
  ERR_TRY(nvs_store_open(PARTITION, NAMESPACE, &ns));
  return nvs_store_set(ns, KEY_ID, &id, sizeof(id));
}

uint32_t board_boot_id(void) {
  return s_board.boot_id;
}

uint32_t board_build_id(void) {
  return s_board.build_id;
}
