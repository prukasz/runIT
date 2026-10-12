#include "sys_power_manager_router.h"
#include "sys_power_manager.h"

typedef struct __attribute__((packed)) {
  uint16_t target;  //@alias Target @description The board or one rail (a device id) @id device @any 65535
  uint8_t kind;  //@alias Event @enum-ref sys_power_kind_e
  uint8_t response;  //@alias Response @enum-ref sys_power_response_e
} response_args_t;

typedef struct __attribute__((packed)) {
  uint16_t target;  //@alias Target @description The board or one rail (a device id) @id device @any 65535
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
    //@op SYS_PWRM_OP_SET_CONFIG @alias Set configuration @description Budget mode, source pin and negotiation
    {SYS_PWRM_OP_SET_CONFIG, op_set_config},
    //@op SYS_PWRM_OP_GET_CONFIG @alias Read configuration @description The power manager's configuration @returns sys_power_manager_cfg_t
    {SYS_PWRM_OP_GET_CONFIG, op_get_config},
    //@op SYS_PWRM_OP_SET_PSU @alias Set power supply @description The limits of a bench supply as the source
    {SYS_PWRM_OP_SET_PSU, op_set_psu},
    //@op SYS_PWRM_OP_SET_BATTERY @alias Set battery @description The battery the board runs on
    {SYS_PWRM_OP_SET_BATTERY, op_set_battery},
    //@op SYS_PWRM_OP_SET_RESPONSE @alias Set response @description What happens on one power event of the board or a rail
    {SYS_PWRM_OP_SET_RESPONSE, op_set_response},
    //@op SYS_PWRM_OP_GET_RESPONSES @alias Read responses @description The response to every power event of the board or a rail @returns u8[] @returns-alias Responses @returns-description One response per power event, in the order of the power event list
    {SYS_PWRM_OP_GET_RESPONSES, op_get_responses},
    //@op SYS_PWRM_OP_GET_STATUS @alias Status @description Source, input, budget, allocation and battery @returns sys_power_status_t
    {SYS_PWRM_OP_GET_STATUS, op_get_status},
    //@op SYS_PWRM_OP_NEGOTIATE_MAX @alias Negotiate maximum @description Asks a USB-PD charger for its highest offer
    {SYS_PWRM_OP_NEGOTIATE_MAX, op_negotiate_max},
    //@op SYS_PWRM_OP_SET_RESPONSES @alias Set all responses @description The whole response matrix at once @tail u8[] @tail-alias Responses @tail-description A row per target (the board, then each rail), each with one response per power event
    {SYS_PWRM_OP_SET_RESPONSES, op_set_responses},
};

ROUTER_MODULE(sys_power_manager_router, k_ops)
