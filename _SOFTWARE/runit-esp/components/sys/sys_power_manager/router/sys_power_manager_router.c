#include "sys_power_manager_router.h"
#include "sys_power_manager.h"

ROUTER_OP(op_set_config, sys_power_manager_cfg_t) {
  return sys_power_manager_set_config(args);
}

ROUTER_OP_NOARGS(op_get_config) {
  sys_power_manager_cfg_t cfg;
  sys_power_manager_get_config(&cfg);
  return cmd_reply(out, &cfg, sizeof(cfg));
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
  return cmd_reply(out, &psu, sizeof(psu));
}

ROUTER_OP_NOARGS(op_get_battery) {
  sys_power_battery_t battery;
  sys_power_manager_get_battery(&battery);
  return cmd_reply(out, &battery, sizeof(battery));
}

ROUTER_OP_NOARGS(op_get_matrix) {
  sys_power_response_row_t rows[1 + CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS];
  uint8_t count = 0;
  ERR_TRY(sys_power_manager_get_matrix(rows, (uint8_t)(sizeof(rows) / sizeof(rows[0])), &count));
  return cmd_reply(out, rows, (size_t)count * sizeof(rows[0]));
}

ROUTER_OP_NOARGS(op_get_status) {
  sys_power_status_t st;
  ERR_TRY(sys_power_manager_get_status(&st));
  return cmd_reply(out, &st, sizeof(st));
}

ROUTER_OP(op_set_pd, sys_power_pd_t) {
  return sys_power_manager_set_pd(args);
}

ROUTER_OP_NOARGS(op_get_pd) {
  sys_power_pd_t pd;
  sys_power_manager_get_pd(&pd);
  return cmd_reply(out, &pd, sizeof(pd));
}

// Rows of sys_power_response_row_t as the payload (what the read answers): no header, so not a ROUTER_OP_TAIL. `in` is
// the command's code, then the rows.
static err_h op_set_matrix(CMD_F_PARAMS) {
  (void)ctx;
  (void)out;
  const uint32_t len = in.len - 1;
  ERR_CHECK(len % sizeof(sys_power_response_row_t) == 0, ERR_CMD_BAD_LEN, .got = (uint16_t)len,
            .need = (uint16_t)sizeof(sys_power_response_row_t));
  return sys_power_manager_set_matrix((const sys_power_response_row_t*)(in.data + 1),
                                      (uint8_t)(len / sizeof(sys_power_response_row_t)));
}

static const router_op_t k_ops[] = {
    ROUTER_RW(SYS_PWRM_OP_CONFIG, op_get_config, op_set_config),
    ROUTER_RW(SYS_PWRM_OP_PSU, op_get_psu, op_set_psu),
    ROUTER_RW(SYS_PWRM_OP_BATTERY, op_get_battery, op_set_battery),
    ROUTER_R(SYS_PWRM_OP_STATUS, op_get_status),
    ROUTER_RW(SYS_PWRM_OP_PD, op_get_pd, op_set_pd),
    ROUTER_RW(SYS_PWRM_OP_MATRIX, op_get_matrix, op_set_matrix),
};

ROUTER_MODULE(sys_power_manager_router, k_ops)
