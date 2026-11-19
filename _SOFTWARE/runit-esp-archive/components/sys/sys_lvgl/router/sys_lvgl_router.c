#include "sys_lvgl_router.h"

ROUTER_OP(op_install, sys_lvgl_install_t) {
  (void)ctx;
  (void)out;
  return sys_lvgl_install(args);
}

ROUTER_OP_NOARGS(op_get_install) {
  (void)ctx;
  sys_lvgl_install_t install;
  ERR_TRY(sys_lvgl_get_install(&install));
  return cmd_reply(out, &install, sizeof(install));
}

ROUTER_OP_NOARGS(op_reset) {
  (void)ctx;
  (void)out;
  return sys_lvgl_reset();
}

ROUTER_OP(op_rotation, sys_lvgl_rotation_t) {
  (void)ctx;
  (void)out;
  return sys_lvgl_set_rotation(args->rotation);
}

ROUTER_OP_NOARGS(op_get_rotation) {
  (void)ctx;
  sys_lvgl_rotation_t r;
  ERR_TRY(sys_lvgl_get_rotation(&r.rotation));
  return cmd_reply(out, &r, sizeof(r));
}

ROUTER_OP_NOARGS(op_status) {
  (void)ctx;
  sys_lvgl_status_t status;
  ERR_TRY(sys_lvgl_get_status(&status));
  return cmd_reply(out, &status, sizeof(status));
}

ROUTER_OP_NOARGS(op_start) {
  (void)ctx;
  (void)out;
  return sys_lvgl_start();
}

ROUTER_OP_NOARGS(op_stop) {
  (void)ctx;
  (void)out;
  return sys_lvgl_stop();
}

static const router_op_t k_ops[] = {
    ROUTER_RW(SYS_LVGL_OP_INSTALL, op_get_install, op_install),
    ROUTER_W(SYS_LVGL_OP_RESET, op_reset),
    ROUTER_RW(SYS_LVGL_OP_ROTATION, op_get_rotation, op_rotation),
    ROUTER_R(SYS_LVGL_OP_STATUS, op_status),
    ROUTER_W(SYS_LVGL_OP_START, op_start),
    ROUTER_W(SYS_LVGL_OP_STOP, op_stop),
};

ROUTER_MODULE(sys_lvgl_router, k_ops)
