/**
 * @file
 * @brief Remote interface of sys_render: the ops under the root key MODULE_ID_RENDER (packed,
 *        little-endian). The router registers itself (ROUTER_MODULE_REGISTER), so no component
 *        names it. It only calls the API of sys_render.h, with no logic of its own.
 */
#pragma once

#include "router_table.h"
#include "sys_render/sys_render.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h). Op codes are
 * wire format: never renumber.
 *   OP_INSTALL:  read answers the install (sys_render_get_install), write installs
 *   OP_RESET:    write resets
 *   OP_ROTATION: read answers the rotation, write turns the display
 *   OP_START:    write starts drawing
 *   OP_STOP:     write stops drawing
 */
#define RENDER_OPERATIONS(X, P)                                                                    \
    X(P, OP_INSTALL, 0x01, cmd_none_t, sys_render_install_t, 0)                                    \
    X(P, OP_RESET, 0x02, cmd_none_t, cmd_none_t, 0)                                                \
    X(P, OP_ROTATION, 0x03, cmd_none_t, sys_render_rotation_t, 0)                                  \
    X(P, OP_START, 0x04, cmd_none_t, cmd_none_t, 0)                                                \
    X(P, OP_STOP, 0x05, cmd_none_t, cmd_none_t, 0)

CMD_TABLE_DECLARE(RENDER)

/** @brief The remote interface of the render, under the root key MODULE_ID_RENDER. */
ROUTER_MODULE_DECLARE(sys_render_router);

#ifdef __cplusplus
}
#endif
