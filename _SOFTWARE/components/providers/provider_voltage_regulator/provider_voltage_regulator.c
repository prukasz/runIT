#include "provider_voltage_regulator.h"
#include "tps55289.h"
#include "tps55289_mock.h"

static void* tps_dev_handle_0 = NULL;
static void* tps_dev_handle_1 = NULL;

status_rep_t sys_pwr_init_reg(void* tps_dev0, void* tps_dev1){ 
    tps_dev_handle_0 = tps_dev0;
    tps_dev_handle_1 = tps_dev1;
    //turn off at start
    tps55289_set_output_enable(tps_dev_handle_0, false);
    tps55289_set_output_enable(tps_dev_handle_1, false);
    tps55289_set_current_limit(tps_dev_handle_0, 100, true);
    tps55289_set_current_limit(tps_dev_handle_1, 100, true);
    return STA_OK;
}

status_rep_t sys_pwr_set_reg_voltage(bool reg_num, uint32_t voltage_mv){
    tps55289_handle_t handle = (reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1;
    return STA_FROM_ESP(tps55289_set_voltage(handle, voltage_mv), OWNER_PROVIDER_VREG_SET_VOLTAGE, reg_num);
}

status_rep_t sys_pwr_set_reg_current_limit(bool reg_num, uint32_t limit_ma){
    tps55289_handle_t handle = (reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1;
    return STA_FROM_ESP(tps55289_set_current_limit(handle, true, limit_ma), OWNER_PROVIDER_VREG_SET_CURRENT_LIMIT, reg_num);
}

status_rep_t sys_pwr_en_reg(bool reg_num, bool state){
    tps55289_handle_t handle = (reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1;
    return STA_FROM_ESP(tps55289_set_output_enable(handle, state), OWNER_PROVIDER_VREG_EN, reg_num);
}

status_rep_t sys_pwr_reg_register_ocp_callback(bool reg_num, void (*callback)(void*), void* ctx){
    tps55289_register_user_callback((reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1, TPS55289_FAULT_OCP, callback, ctx);
    return STA_FROM_ESP(tps55289_set_fault_masks((reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1, false, callback ? true : false, false), OWNER_PROVIDER_VREG_REGISTER_OCP_CALLBACK, reg_num);
}

status_rep_t sys_pwr_reg_register_ovp_callback(bool reg_num, void (*callback)(void*), void* ctx){
    tps55289_register_user_callback((reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1, TPS55289_FAULT_OVP, callback, ctx);
    return STA_FROM_ESP(tps55289_set_fault_masks((reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1, false, false, callback ? true : false), OWNER_PROVIDER_VREG_REGISTER_OVP_CALLBACK, reg_num);
}

status_rep_t sys_pwr_reg_register_scp_callback(bool reg_num, void (*callback)(void*), void* ctx){
    tps55289_register_user_callback((reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1, TPS55289_FAULT_SCP, callback, ctx);
    return STA_FROM_ESP(tps55289_set_fault_masks((reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1, callback ? true : false, false, false), OWNER_PROVIDER_VREG_REGISTER_SCP_CALLBACK, reg_num);
}
