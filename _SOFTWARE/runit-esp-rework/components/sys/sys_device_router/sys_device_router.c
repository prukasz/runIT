#include "sys_device_router.h"

#include <string.h>

#include "sys_device.h"
#include "sys_device_hooks.h"

// A write of one of these lifecycle ops to SYS_DEVICE_ID_NONE is a sweep: the op on every device.
static bool is_sweep(const sys_device_router_hdr_t* h) {
  return h->id == SYS_DEVICE_ID_NONE && h->dir == CMD_DIR_WRITE &&
         (h->op == CONTRACT_SYS_DEVICE_OP_SUSPEND || h->op == CONTRACT_SYS_DEVICE_OP_RESUME ||
          h->op == CONTRACT_SYS_DEVICE_OP_RESET || h->op == CONTRACT_SYS_DEVICE_OP_REMOVE);
}

// The command is a sys_device_call: the same code runs for a command and for a call from the firmware, but an op
// marked local (SYS_DEVICE_OP_LOCAL) is refused here (sys_device_call_routed).
err_h sys_device_router(CMD_F_PARAMS) {
  (void)ctx;
  const uint32_t got = in.len > 0 ? in.len - 1 : 0;  // after the key
  ERR_CHECK(got >= sizeof(sys_device_router_hdr_t), ERR_CMD_BAD_LEN, .got = (uint16_t)got,
            .need = sizeof(sys_device_router_hdr_t));
  sys_device_router_hdr_t h;
  memcpy(&h, in.data + 1, sizeof(h));  // little-endian target
  const cmd_in_t args = {.data = in.data + 1 + sizeof(h), .len = got - sizeof(h)};
  if (is_sweep(&h)) {
    return sys_device_sweep(h.op);
  }
  return sys_device_call_routed(h.id, h.dir, h.op, args, out);
}

ROUTER_MODULE_REGISTER(MODULE_ID_SYS_DEVICE, sys_device_router)
  