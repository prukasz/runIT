#pragma once

#include "router_table.h"
#include "sys_lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The module's one number: the root key of its commands (the module of its errors is the same, sys_lvgl_errors.h). */

/* Ops under the root key MODULE_ID_LVGL (SYS_LVGL.MD "Commands"), each with the frame it uses (packed, little-endian;
 * contract_lvgl.json names them). Never renumber. The router registers itself (ROUTER_MODULE_REGISTER). */

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h). Sides: INSTALL RW, RESET W,
 * ROTATION RW, STATUS R, START W, STOP W.
 */
#define LVGL_OPERATIONS(X, P)                                 \
  X(P, OP_INSTALL,  0x01, cmd_none_t, sys_lvgl_install_t,  0) \
  X(P, OP_RESET,    0x02, cmd_none_t, cmd_none_t,          0) \
  X(P, OP_ROTATION, 0x03, cmd_none_t, sys_lvgl_rotation_t, 0) \
  X(P, OP_STATUS,   0x04, cmd_none_t, sys_lvgl_status_t,   0) \
  X(P, OP_START,    0x05, cmd_none_t, cmd_none_t,          0) \
  X(P, OP_STOP,     0x06, cmd_none_t, cmd_none_t,          0)

CMD_TABLE_DECLARE(LVGL)

/** @brief Router of MODULE_ID_LVGL: the LVGL adapter's commands. */
ROUTER_MODULE_DECLARE(sys_lvgl_router);

#ifdef __cplusplus
}
#endif
