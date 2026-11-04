#include "sys_data_provider_router.h"

#include "sdkconfig.h"
#include "sys_data_provider_driver.h"
#include "sys_data_provider_internal.h"

ROUTER_OP(op_remove, sys_data_provider_id_t) {
  return sys_data_provider_remove(*args);
}

ROUTER_OP_NOARGS(op_list) {
  sys_data_provider_info_t list[CONFIG_SYS_DATA_PROVIDER_MAX];
  const uint8_t count = sys_data_provider_list(list);
  ERR_TRY(cmd_append(out, &count, sizeof(count)));
  return cmd_append(out, list, count * sizeof(list[0]));
}

ROUTER_OP_TAIL(op_send, sys_data_provider_id_t) {
  return sys_data_provider_send_free(*args, tail.data, tail.len);
}

ROUTER_OP(op_bind_commands, sys_data_provider_id_t) {
  return sys_data_provider_bind_commands(*args);
}

ROUTER_OP(op_unbind_commands, sys_data_provider_id_t) {
  return sys_data_provider_unbind_commands(*args);
}

static const router_op_t k_ops[] = {
    ROUTER_W(SYS_DATA_PROVIDER_OP_REMOVE, op_remove),
    ROUTER_R(SYS_DATA_PROVIDER_OP_LIST, op_list),
    ROUTER_W(SYS_DATA_PROVIDER_OP_SEND, op_send),
    ROUTER_W(SYS_DATA_PROVIDER_OP_BIND_COMMANDS, op_bind_commands),
    ROUTER_W(SYS_DATA_PROVIDER_OP_UNBIND_COMMANDS, op_unbind_commands),
};

ROUTER_MODULE(sys_data_provider_router, k_ops)
