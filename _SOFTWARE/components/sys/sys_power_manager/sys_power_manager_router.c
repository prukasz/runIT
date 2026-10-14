#include "sys_power_manager_router.h"
#include "sys_power_manager.h"

typedef struct __attribute__((packed)) {
  uint16_t target;   /**< Target board (SYS_POWER_TARGET_BOARD) or rail device ID */
  uint8_t kind;      /**< sys_power_kind_e */
  uint8_t response;  /**< sys_power_response_e */
} response_args_t;

typedef struct __attribute__((packed)) {
  uint16_t target;   /**< Target board (SYS_POWER_TARGET_BOARD) or rail device ID */
} target_args_t;

ROUTER_OP(op_set_config, sys_power_manager_cfg_t) {
  return sys_power_manager_set_config(args);
}

ROUTER_OP_NOARGS(op_get_config) {
  sys_power_manager_cfg_t cfg;
  sys_power_manager_get_config(&cfg);
  return cmd_reply(out, out_cap, out_len, &cfg, sizeof(cfg));
}

ROUTER_OP(op_set_psu, sys_power_psu_t) {
  return sys_power_manager_set_psu(args);
}

ROUTER_OP(op_set_battery, sys_power_battery_t) {
  return sys_power_manager_set_battery(args);
}

ROUTER_OP(op_set_response, response_args_t) {
  return sys_power_manager_set_response(args->target, args->kind, args->response);
}

ROUTER_OP(op_get_responses, target_args_t) {
  uint8_t r[SYS_POWER_KIND_COUNT];
  ERR_TRY(sys_power_manager_get_responses(args->target, r));
  return cmd_reply(out, out_cap, out_len, r, sizeof(r));
}

ROUTER_OP_NOARGS(op_get_status) {
  sys_power_status_t st;
  ERR_TRY(sys_power_manager_get_status(&st));
  return cmd_reply(out, out_cap, out_len, &st, sizeof(st));
}

ROUTER_OP_NOARGS(op_negotiate_max) {
  return sys_power_manager_negotiate_max();
}

// The whole matrix as the payload: no header, so not a ROUTER_OP_TAIL. `in` is the op's key, then the matrix.
static err_h op_set_responses(CMD_F_PARAMS) {
  (void)ctx;
  (void)out;
  (void)out_cap;
  (void)out_len;
  return sys_power_manager_set_responses(in + 1, in_len - 1);
}

static const router_entry_t k_ops[] = {
    {SYS_PWRM_OP_SET_CONFIG, op_set_config},
    {SYS_PWRM_OP_GET_CONFIG, op_get_config},
    {SYS_PWRM_OP_SET_PSU, op_set_psu},
    {SYS_PWRM_OP_SET_BATTERY, op_set_battery},
    {SYS_PWRM_OP_SET_RESPONSE, op_set_response},
    {SYS_PWRM_OP_GET_RESPONSES, op_get_responses},
    {SYS_PWRM_OP_GET_STATUS, op_get_status},
    {SYS_PWRM_OP_NEGOTIATE_MAX, op_negotiate_max},
    {SYS_PWRM_OP_SET_RESPONSES, op_set_responses},
};

ROUTER_MODULE(sys_power_manager_router, k_ops)
