#include "provider_voltage_regulator.h"
#include "tps55289.h"
#include "tps55289_mock.h"

static void* tps_dev_handle_0 = NULL;
static void* tps_dev_handle_1 = NULL;

#define CHECK_HANDLE(VAL, num) do { if (!(VAL)) return STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_PROVIDER_VREG, (num)); } while (0)


void* p_vreg_0_new(void){
    tps_dev_handle_0 = tps55289_new(0x74);
    return tps_dev_handle_0;
}

void* p_vreg_1_new(void){
    tps_dev_handle_1 = tps55289_new(0x75);
    return tps_dev_handle_1;
}

i2c_device_config_t* p_vreg_get_i2c_dev_config(bool reg_num){
    tps55289_handle_t handle = (reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1;
    return &handle->i2c_device_config;
}
i2c_master_dev_handle_t*p_vreg_get_i2c_dev_handle(bool reg_num){
    tps55289_handle_t handle = (reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1;
    return &handle->i2c_master_dev_handle;
}
TaskHandle_t p_vreg_get_task_handle(bool reg_num){
    tps55289_handle_t handle = (reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1;
    return handle->driver_task_handle;
}

status_rep_t p_vreg_start(){ 
    if(tps_dev_handle_0){
        STA_FROM_ESP(tps55289_set_output_enable(tps_dev_handle_0, false), OWNER_PROVIDER_VREG, 0);
        STA_FROM_ESP(tps55289_set_current_limit(tps_dev_handle_0, 100, true), OWNER_PROVIDER_VREG, 0);
    }
    if(tps_dev_handle_1){
        STA_FROM_ESP(tps55289_set_output_enable(tps_dev_handle_1, false), OWNER_PROVIDER_VREG, 1);
        STA_FROM_ESP(tps55289_set_current_limit(tps_dev_handle_1, 100, true), OWNER_PROVIDER_VREG, 1);
    }
    return STA_OK;
}

status_rep_t p_vreg_set_voltage(bool reg_num, uint32_t voltage_mv){
    tps55289_handle_t handle = (reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1;
    CHECK_HANDLE(handle, reg_num);
    return STA_FROM_ESP(tps55289_set_voltage(handle, voltage_mv), OWNER_PROVIDER_VREG, reg_num);
}

status_rep_t p_vreg_set_current_limit(bool reg_num, uint32_t limit_ma){
    tps55289_handle_t handle = (reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1;
    CHECK_HANDLE(handle, reg_num);
    return STA_FROM_ESP(tps55289_set_current_limit(handle, true, limit_ma), OWNER_PROVIDER_VREG, reg_num);
}

status_rep_t p_vreg_en(bool reg_num, bool state){
    tps55289_handle_t handle = (reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1;
    CHECK_HANDLE(handle, reg_num);
    return STA_FROM_ESP(tps55289_set_output_enable(handle, state), OWNER_PROVIDER_VREG, reg_num);
}

status_rep_t p_vreg_register_ocp_callback(bool reg_num, void (*callback)(void*), void* ctx){
    tps55289_handle_t handle = (reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1;
    CHECK_HANDLE(handle, reg_num);
    tps55289_register_user_callback(handle, TPS55289_FAULT_OCP, callback, ctx);
    return STA_FROM_ESP(tps55289_set_fault_masks(handle, false, callback ? true : false, false), OWNER_PROVIDER_VREG, reg_num);
}

status_rep_t p_vreg_register_ovp_callback(bool reg_num, void (*callback)(void*), void* ctx){
    tps55289_handle_t handle = (reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1;
    CHECK_HANDLE(handle, reg_num);
    tps55289_register_user_callback(handle, TPS55289_FAULT_OVP, callback, ctx);
    return STA_FROM_ESP(tps55289_set_fault_masks(handle, false, false, callback ? true : false), OWNER_PROVIDER_VREG, reg_num);
}

status_rep_t p_vreg_register_scp_callback(bool reg_num, void (*callback)(void*), void* ctx){
    tps55289_handle_t handle = (reg_num == 0) ? tps_dev_handle_0 : tps_dev_handle_1;
    CHECK_HANDLE(handle, reg_num);
    tps55289_register_user_callback(handle, TPS55289_FAULT_SCP, callback, ctx);
    return STA_FROM_ESP(tps55289_set_fault_masks(handle, callback ? true : false, false, false), OWNER_PROVIDER_VREG, reg_num);
}
