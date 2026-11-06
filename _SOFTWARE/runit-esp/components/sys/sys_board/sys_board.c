#include "sys_board.h"

#include "sdkconfig.h"
#include "sys_actions_router.h"

#define JSON_NAMESPACE "board"

static sys_nvs_json_t s_json;

err_h sys_board_init(void) {
  return sys_nvs_json_open(&s_json, CONFIG_SYS_ACTIONS_PARTITION, JSON_NAMESPACE);
}

sys_nvs_json_t* sys_board_json(void) {
  return &s_json;
}

err_h sys_board_boot(void) {
  sys_actions_info_t info;
  err_h err = sys_actions_info(SYS_BOARD_RECORD_ID, &info);
  if (err != NULL && err->tag == ERR_ACTION_NOT_FOUND) {
    err_free(err);
    return NULL;
  }
  ERR_TRY(err);
  const uint32_t id = SYS_BOARD_RECORD_ID;
  const uint8_t frame[] = {ROUTER_KEY_ACTIONS, CMD_DIR_WRITE, SYS_ACTIONS_OP_RUN_ALL,
                           (uint8_t)id, (uint8_t)(id >> 8), (uint8_t)(id >> 16), (uint8_t)(id >> 24)};
  return router_inject((cmd_in_t){frame, sizeof(frame)}, 0, NULL, NULL);
}
