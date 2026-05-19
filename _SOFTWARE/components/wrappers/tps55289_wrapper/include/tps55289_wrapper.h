#pragma once
#include "status.h"

status_rep_t sys_pwr_init_reg(void* tps_dev0, void* tps_dev1);
status_rep_t sys_pwr_set_reg_voltage(bool reg_num, uint32_t voltage_mv);
status_rep_t sys_pwr_set_reg_current_limit(bool reg_num, uint32_t current_ma);
status_rep_t sys_pwr_en_reg(bool reg_num, bool state);

extern void tps55289_isr_callback_fault(void *arg);
