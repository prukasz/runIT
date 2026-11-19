#include "sys_board.h"

#include <string.h>

#include "router_queue.h"
#include "sdkconfig.h"
#include "sys_actions.h"
#include "sys_actions_router.h"
#include "sys_board_internal.h"
#include "sys_board_router.h"

#define JSON_NAMESPACE "board"

static nvs_store_json_t s_json;

err_h sys_board_init(void) {
  return nvs_store_json_open(&s_json, CONFIG_SYS_ACTIONS_PARTITION, JSON_NAMESPACE);
}

nvs_store_json_t* sys_board_json(void) {
  return &s_json;
}

err_h sys_board_boot(void) {
  sys_actions_info_t info;
  err_h err = sys_actions_info(SYS_ACTIONS_ID_BOARD, &info);
  if (err != NULL && err_root_tag(err) == ERR_ACTION_NOT_FOUND) {
    err_free(err);
    return NULL;  // a board with no profile: its own GPIO only
  }
  ERR_TRY(err);
  const uint32_t id = SYS_ACTIONS_ID_BOARD;
  uint8_t req[3 + sizeof(id)] = {MODULE_ID_ACTIONS, CMD_DIR_WRITE, ACTIONS_OP_RUN_ALL};
  memcpy(req + 3, &id, sizeof(id));
  return router_inject((cmd_in_t){req, sizeof(req)}, 0, NULL, NULL);
}
