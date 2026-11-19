#include "sys_flasher_router.h"
#include "sys_flasher.h"

ROUTER_OP_NOARGS(op_start) {
  return sys_flasher_start();
}

ROUTER_OP_NOARGS(op_abort) {
  sys_flasher_abort();
  return NULL;
}

ROUTER_OP_NOARGS(op_status) {
  const sys_flasher_status_t status = sys_flasher_status();
  const sys_flasher_status_reply_t reply = {
      .state = status.state,
      .step = status.step,
      .done = status.done,
      .total = status.total,
      .error = status.error,
  };
  return cmd_reply(out, &reply, sizeof(reply));
}

static const router_op_t k_ops[] = {
    ROUTER_W(SYS_FLASHER_OP_START, op_start),
    ROUTER_R(SYS_FLASHER_OP_STATUS, op_status),
    ROUTER_W(SYS_FLASHER_OP_ABORT, op_abort),
};

ROUTER_MODULE(sys_flasher_router, k_ops)
