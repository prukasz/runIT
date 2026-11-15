#include "sys_data_provider_router.h"

#include <string.h>

#include "sys_data_provider.h"
#include "sys_data_provider_internal.h"

// Forwards a command under root key MODULE_ID_PROVIDER (0x0D) to the typed provider registry, blocking LOCAL ops.
err_h sys_data_provider_router(CMD_F_PARAMS) {
  (void)ctx;
  const uint32_t got = in.len > 0 ? in.len - 1u : 0u;  // after the key
  ERR_CHECK(got >= sizeof(sys_data_provider_router_hdr_t), ERR_CMD_BAD_LEN, .got = (uint16_t)got,
            .need = sizeof(sys_data_provider_router_hdr_t));
  sys_data_provider_router_hdr_t h;
  memcpy(&h, in.data + 1u, sizeof(h));  // little-endian target
  const cmd_in_t args = {.data = in.data + 1u + sizeof(h), .len = got - sizeof(h)};
  return sys_data_provider_call_routed(h.id, h.dir, h.op, args, out);
}

ROUTER_MODULE_REGISTER(PROVIDER, sys_data_provider_router)
