#include "sys_lvgl_router.h"

MODULE_ID_DEFINE(LVGL, 0x29)

ROUTER_GETTER(LVGL, OP_INSTALL, sys_lvgl_get_install)
ROUTER_SETTER(LVGL, OP_INSTALL, sys_lvgl_install)

ROUTER_WRITE(LVGL, OP_RESET) {
  return sys_lvgl_reset();
}

ROUTER_READ(LVGL, OP_ROTATION) {
  sys_lvgl_rotation_t rotation;
  ERR_TRY(sys_lvgl_get_rotation(&rotation.rotation));
  return cmd_reply(out, &rotation, sizeof(rotation));
}

ROUTER_WRITE(LVGL, OP_ROTATION) {
  return sys_lvgl_set_rotation(args->rotation);
}

ROUTER_GETTER(LVGL, OP_STATUS, sys_lvgl_get_status)

ROUTER_WRITE(LVGL, OP_START) {
  return sys_lvgl_start();
}

ROUTER_WRITE(LVGL, OP_STOP) {
  return sys_lvgl_stop();
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY(LVGL, OP_INSTALL), ROUTER_ENTRY(LVGL, OP_RESET), ROUTER_ENTRY(LVGL, OP_ROTATION),
    ROUTER_ENTRY_R(LVGL, OP_STATUS), ROUTER_ENTRY_W(LVGL, OP_START), ROUTER_ENTRY_W(LVGL, OP_STOP),
};

ROUTER_MODULE(sys_lvgl_router, k_ops)
ROUTER_MODULE_REGISTER(LVGL, sys_lvgl_router)
