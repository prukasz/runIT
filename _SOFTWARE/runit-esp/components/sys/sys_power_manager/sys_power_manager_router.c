#include "sys_power_manager_router.h"
#include "sys_power_manager.h"

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

ROUTER_OP_NOARGS(op_get_psu) {
  sys_power_psu_t psu;
  sys_power_manager_get_psu(&psu);
  return cmd_reply(out, out_cap, out_len, &psu, sizeof(psu));
}

ROUTER_OP_NOARGS(op_get_battery) {
  sys_power_battery_t battery;
  sys_power_manager_get_battery(&battery);
  return cmd_reply(out, out_cap, out_len, &battery, sizeof(battery));
}

ROUTER_OP_NOARGS(op_get_matrix) {
  sys_power_response_row_t rows[1 + CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS];
  uint8_t count = 0;
  ERR_TRY(sys_power_manager_get_matrix(rows, (uint8_t)(sizeof(rows) / sizeof(rows[0])), &count));
  return cmd_reply(out, out_cap, out_len, rows, (size_t)count * sizeof(rows[0]));
}

ROUTER_OP_NOARGS(op_get_status) {
  sys_power_status_t st;
  ERR_TRY(sys_power_manager_get_status(&st));
  return cmd_reply(out, out_cap, out_len, &st, sizeof(st));
}

ROUTER_OP(op_set_pd, sys_power_pd_t) {
  return sys_power_manager_set_pd(args);
}

ROUTER_OP_NOARGS(op_get_pd) {
  sys_power_pd_t pd;
  sys_power_manager_get_pd(&pd);
  return cmd_reply(out, out_cap, out_len, &pd, sizeof(pd));
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
    {SYS_PWRM_OP_GET_STATUS, op_get_status},
    {SYS_PWRM_OP_SET_PD, op_set_pd},
    {SYS_PWRM_OP_SET_RESPONSES, op_set_responses},
    {SYS_PWRM_OP_GET_PSU, op_get_psu},
    {SYS_PWRM_OP_GET_BATTERY, op_get_battery},
    {SYS_PWRM_OP_GET_MATRIX, op_get_matrix},
    {SYS_PWRM_OP_GET_PD, op_get_pd},
};

ROUTER_MODULE(sys_power_manager_router, k_ops)
