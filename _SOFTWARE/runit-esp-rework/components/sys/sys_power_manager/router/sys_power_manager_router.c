#include "sys_power_manager_router.h"

/* The settings: the module's getter and setter, bound by the table. */
ROUTER_GETTER(PWRM, CONFIG, sys_power_manager_get_config)
ROUTER_SETTER(PWRM, CONFIG, sys_power_manager_set_config)
ROUTER_SETTER(PWRM, LAYOUT, sys_power_manager_set_layout)
ROUTER_GETTER(PWRM, PSU, sys_power_manager_get_psu)
ROUTER_SETTER(PWRM, PSU, sys_power_manager_set_psu)
ROUTER_GETTER(PWRM, BATTERY, sys_power_manager_get_battery)
ROUTER_SETTER(PWRM, BATTERY, sys_power_manager_set_battery)
ROUTER_SETTER(PWRM, RAIL, sys_power_manager_add_rail)
ROUTER_GETTER(PWRM, STATUS, sys_power_manager_get_status)
ROUTER_GETTER(PWRM, PD, sys_power_manager_get_pd)
ROUTER_SETTER(PWRM, PD, sys_power_manager_set_pd)

/* MATRIX: rows of its value, as many as the board has (1 + rails), both ways. */
ROUTER_READ(PWRM, MATRIX) {
  PWRM_value_MATRIX_t rows[1 + CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS];
  uint8_t count = 0;
  ERR_TRY(sys_power_manager_get_matrix(rows, (uint8_t)(sizeof(rows) / sizeof(rows[0])), &count));
  return cmd_reply(out, rows, (uint32_t)count * sizeof(rows[0]));
}

// Rows with no header, so not the table's fixed frame: `in` is the rows, a whole number of them.
static err_h PWRM_w_MATRIX(CMD_F_PARAMS) {
  (void)ctx;
  (void)out;
  ERR_CHECK(in.len % sizeof(PWRM_value_MATRIX_t) == 0, ERR_CMD_BAD_LEN, .got = (uint16_t)in.len,
            .need = (uint16_t)sizeof(PWRM_value_MATRIX_t));
  return sys_power_manager_set_matrix((const PWRM_value_MATRIX_t*)in.data,
                                      (uint8_t)(in.len / sizeof(PWRM_value_MATRIX_t)));
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY(PWRM, CONFIG),  ROUTER_ENTRY_W(PWRM, LAYOUT), ROUTER_ENTRY(PWRM, PSU),
    ROUTER_ENTRY(PWRM, BATTERY), ROUTER_ENTRY_W(PWRM, RAIL),   ROUTER_ENTRY_R(PWRM, STATUS),
    ROUTER_ENTRY(PWRM, PD),      ROUTER_ENTRY(PWRM, MATRIX),
};

ROUTER_MODULE(sys_power_manager_router, k_ops)

ROUTER_MODULE_REGISTER(PWRM, sys_power_manager_router)
