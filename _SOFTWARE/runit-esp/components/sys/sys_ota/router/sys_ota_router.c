#include "sys_ota_router.h"
#include "sys_ota.h"

ROUTER_OP(op_begin, sys_ota_begin_args_t) {
  return sys_ota_begin(args->size);
}

ROUTER_OP_TAIL(op_write, sys_ota_write_args_t) {
  return sys_ota_write(args->offset, tail.data, tail.len);
}

ROUTER_OP_NOARGS(op_commit) {
  return sys_ota_commit();
}

ROUTER_OP_NOARGS(op_abort) {
  sys_ota_abort();
  return NULL;
}

ROUTER_OP_NOARGS(op_confirm) {
  return sys_ota_confirm();
}

ROUTER_OP_NOARGS(op_info) {
  const sys_ota_info_t info = sys_ota_info();
  const sys_ota_info_reply_t reply = {
      .slot = info.slot,
      .state = info.state,
      .open = info.open ? 1u : 0u,
      .written = info.written,
      .slot_bytes = info.slot_bytes,
  };
  return cmd_reply(out, &reply, sizeof(reply));
}

static const router_op_t k_ops[] = {
    ROUTER_W(SYS_OTA_OP_BEGIN, op_begin),
    ROUTER_W(SYS_OTA_OP_WRITE, op_write),
    ROUTER_W(SYS_OTA_OP_COMMIT, op_commit),
    ROUTER_W(SYS_OTA_OP_ABORT, op_abort),
    ROUTER_R(SYS_OTA_OP_INFO, op_info),
    ROUTER_W(SYS_OTA_OP_CONFIRM, op_confirm),
};

ROUTER_MODULE(sys_ota_router, k_ops)
