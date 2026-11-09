#pragma once

#include "router.h"
#include "sys_lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_LVGL (SYS_LVGL.MD "Commands"), each with the frame it uses (packed, little-endian;
 * contract_lvgl.json names them) and its sides. Never renumber. */

#define SYS_LVGL_OP_INSTALL  0x01u  // W: sys_lvgl_install_t; R: -> sys_lvgl_install_t
#define SYS_LVGL_OP_RESET    0x02u  // W: no args
#define SYS_LVGL_OP_ROTATION 0x03u  // W: sys_lvgl_rotation_t; R: -> sys_lvgl_rotation_t
#define SYS_LVGL_OP_STATUS   0x04u  // R: -> sys_lvgl_status_t
#define SYS_LVGL_OP_START    0x05u  // W: no args
#define SYS_LVGL_OP_STOP     0x06u  // W: no args

/**
 * @brief The LVGL adapter, under ROUTER_KEY_LVGL. Only components/app/runit (and the PC simulator) includes it, to
 * register it.
 * @usage ERR_TRY(router_register(ROUTER_KEY_LVGL, sys_lvgl_router));
 */
ROUTER_MODULE_DECLARE(sys_lvgl_router);

#ifdef __cplusplus
}
#endif
