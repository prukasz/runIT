#include "provider_voltage_regulator.h"
#include "tps55289.h"

#define TAG __FILE_NAME__
#undef OWNER
#define OWNER OWNER_PROVIDER_VREG
static tps55289_handle_t vreg_handle_0 = NULL;
static tps55289_handle_t vreg_handle_1 = NULL;


void* p_vreg_0_new(void){
    vreg_handle_0 = tps55289_new(CONFIG_I2C_ADDR_TPS55289_0);
    return vreg_handle_0;
}

void* p_vreg_1_new(void){
    vreg_handle_1 = tps55289_new(CONFIG_I2C_ADDR_TPS55289_1);
    return vreg_handle_1;
}

i2c_device_config_t* p_vreg_get_i2c_dev_config(bool reg_num){
    if (reg_num == 0) return vreg_handle_0 ? &(vreg_handle_0->i2c_device_config) : NULL;
    return vreg_handle_1 ? &(vreg_handle_1->i2c_device_config) : NULL;
}
i2c_master_dev_handle_t*p_vreg_get_i2c_dev_handle(bool reg_num){
    if (reg_num == 0) return vreg_handle_0 ? &(vreg_handle_0->i2c_master_dev_handle): NULL;
    return vreg_handle_1 ? &(vreg_handle_1->i2c_master_dev_handle): NULL;
}
TaskHandle_t p_vreg_get_task_handle(bool reg_num){
    if (reg_num == 0) return vreg_handle_0 ? vreg_handle_0->driver_task_handle : NULL;
    return vreg_handle_1 ? vreg_handle_1->driver_task_handle : NULL;
}

status_rep_t p_vreg_start(){ 
    if(vreg_handle_0){
        CHECK_ESP_CALL_R(tps55289_set_output_enable(vreg_handle_0, false));
        CHECK_ESP_CALL_R(tps55289_set_current_limit(vreg_handle_0, 100, true));
    }
    if(vreg_handle_1){
        CHECK_ESP_CALL_R(tps55289_set_output_enable(vreg_handle_1, false));
        CHECK_ESP_CALL_R(tps55289_set_current_limit(vreg_handle_1, 100, true));
    }
    return STA_OK;
}

status_rep_t p_vreg_set_voltage(bool reg_num, uint32_t voltage_mv){
    if (reg_num == 0) {
        CHECK_HANDLE_R(vreg_handle_0);
        CHECK_ESP_CALL_R(tps55289_set_voltage(vreg_handle_0, voltage_mv));
    } else {
        CHECK_HANDLE_R(vreg_handle_1);
        CHECK_ESP_CALL_R(tps55289_set_voltage(vreg_handle_1, voltage_mv));
    }
    return STA_OK;
}

status_rep_t p_vreg_set_current_limit(bool reg_num, uint32_t limit_ma){
    if (reg_num == 0) {
        CHECK_HANDLE_R(vreg_handle_0);
        CHECK_ESP_CALL_R(tps55289_set_current_limit(vreg_handle_0, true, limit_ma));
    } else {
        CHECK_HANDLE_R(vreg_handle_1);
        CHECK_ESP_CALL_R(tps55289_set_current_limit(vreg_handle_1, true, limit_ma));
    }
    return STA_OK;
}

status_rep_t p_vreg_en(bool reg_num, bool state){
    if (reg_num == 0) {
        CHECK_HANDLE_R(vreg_handle_0);
        CHECK_ESP_CALL_R(tps55289_set_output_enable(vreg_handle_0, state));
    } else {
        CHECK_HANDLE_R(vreg_handle_1);
        CHECK_ESP_CALL_R(tps55289_set_output_enable(vreg_handle_1, state));
    }
    return STA_OK;
}

status_rep_t p_vreg_register_ocp_callback(bool reg_num, void (*callback)(void*), void* ctx){
    if (reg_num == 0) {
        CHECK_HANDLE_R(vreg_handle_0);
        tps55289_register_user_callback(vreg_handle_0, TPS55289_FAULT_OCP, callback, ctx);
        CHECK_ESP_CALL_R(tps55289_set_fault_masks(vreg_handle_0, false, callback ? true : false, false));
    } else {
        CHECK_HANDLE_R(vreg_handle_1);
        tps55289_register_user_callback(vreg_handle_1, TPS55289_FAULT_OCP, callback, ctx);
        CHECK_ESP_CALL_R(tps55289_set_fault_masks(vreg_handle_1, false, callback ? true : false, false));
    }
    return STA_OK;
}

status_rep_t p_vreg_register_ovp_callback(bool reg_num, void (*callback)(void*), void* ctx){
    if (reg_num == 0) {
        CHECK_HANDLE_R(vreg_handle_0);
        tps55289_register_user_callback(vreg_handle_0, TPS55289_FAULT_OVP, callback, ctx);
        CHECK_ESP_CALL_R(tps55289_set_fault_masks(vreg_handle_0, false, false, callback ? true : false));
    } else {
        CHECK_HANDLE_R(vreg_handle_1);
        tps55289_register_user_callback(vreg_handle_1, TPS55289_FAULT_OVP, callback, ctx);
        CHECK_ESP_CALL_R(tps55289_set_fault_masks(vreg_handle_1, false, false, callback ? true : false));
    }
    return STA_OK;
}

status_rep_t p_vreg_register_scp_callback(bool reg_num, void (*callback)(void*), void* ctx){
    if (reg_num == 0) {
        CHECK_HANDLE_R(vreg_handle_0);
        tps55289_register_user_callback(vreg_handle_0, TPS55289_FAULT_SCP, callback, ctx);
        CHECK_ESP_CALL_R(tps55289_set_fault_masks(vreg_handle_0, callback ? true : false, false, false));
    } else {
        CHECK_HANDLE_R(vreg_handle_1);
        tps55289_register_user_callback(vreg_handle_1, TPS55289_FAULT_SCP, callback, ctx);
        CHECK_ESP_CALL_R(tps55289_set_fault_masks(vreg_handle_1, callback ? true : false, false, false));
    }
    return STA_OK;
}
