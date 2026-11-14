#include "sys_data_provider_router.h"

#include "sdkconfig.h"
#include "sys_data_provider_driver.h"
#include "sys_data_provider_internal.h"

ROUTER_WRITE(PROVIDER, OP_REMOVE) {
  return sys_data_provider_remove(args->provider);
}

// LIST: u8 count, then rows of its value, one per provider.
ROUTER_READ(PROVIDER, OP_LIST) {
  PROVIDER_value_OP_LIST_t list[CONFIG_SYS_DATA_PROVIDER_MAX];
  const uint8_t count = sys_data_provider_list(list);
  ERR_TRY(cmd_append(out, &count, sizeof(count)));
  return cmd_append(out, list, count * sizeof(list[0]));
}

// SEND: the head names the provider, the tail is the frame, sent as one part.
ROUTER_WRITE_TAIL(PROVIDER, OP_SEND) {
  return sys_data_provider_send(args->provider, NULL, 0, tail, tail_len, 0);
}

ROUTER_WRITE(PROVIDER, OP_BIND_COMMANDS) {
  return sys_data_provider_bind_commands(args->provider);
}

ROUTER_WRITE(PROVIDER, OP_UNBIND_COMMANDS) {
  return sys_data_provider_unbind_commands(args->provider);
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY_W(PROVIDER, OP_REMOVE),        ROUTER_ENTRY_R(PROVIDER, OP_LIST),
    ROUTER_ENTRY_W(PROVIDER, OP_SEND),          ROUTER_ENTRY_W(PROVIDER, OP_BIND_COMMANDS),
    ROUTER_ENTRY_W(PROVIDER, OP_UNBIND_COMMANDS),
};

ROUTER_MODULE(sys_data_provider_router, k_ops)

ROUTER_MODULE_REGISTER(PROVIDER, sys_data_provider_router)
