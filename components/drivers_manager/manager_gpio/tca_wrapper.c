#include "tca_wrapper.h"
#include "tca6424a.h"

static void * tca_dev_handle = NULL;

void tca_wrapper_init(void* dev) {
    tca_dev_handle = dev;
}

esp_err_t io_sys_led_set(bool led_num, bool state){
    uint32_t pin_mask = (led_num == 0) ? IO_TCA_LED_1 : IO_TCA_LED_2;
    uint32_t pins_state = state ? pin_mask : 0;  
    tca_preset_pins(tca_dev_handle, pin_mask, pins_state, false);
    return ESP_OK;
}       

esp_err_t io_sys_pwm_en(bool state){
    tca_preset_pins(tca_dev_handle, IO_TCA_PWM_nOE, state ? IO_TCA_PWM_nOE : 0UL, false);
    return ESP_OK;
}

esp_err_t io_sys_vreg_en(bool vreg_num, bool state){
    uint32_t pin_mask = (vreg_num == 0) ? IO_TCA_REGA_EN : IO_TCA_REGB_EN;
    uint32_t pins_state = state ? pin_mask : 0;  
    tca_preset_pins(tca_dev_handle, pin_mask, pins_state, false);
    return ESP_OK;
}

esp_err_t io_sys_drv_ocpm(bool mode){
    tca_preset_pins(tca_dev_handle, IO_TCA_DRV_OCPM, mode ? IO_TCA_DRV_OCPM : 0UL, false);
    return ESP_OK;
}

esp_err_t io_sys_drv_sleep(bool drv_num, bool state){
    uint32_t pin_mask = (drv_num == 0) ? IO_TCA_DRV1_SLEEP : IO_TCA_DRV2_SLEEP;
    uint32_t pins_state = state ? pin_mask : 0;  
    tca_preset_pins(tca_dev_handle, pin_mask, pins_state, false);
    return ESP_OK;
}

esp_err_t io_sys_drv_set_suplly(bool drv_num, bool sup_num){
    uint32_t pin_mask;
    if(drv_num == 0){
        pin_mask = (sup_num == 0) ? IO_TCA_DRV1_VMA_VSUP : IO_TCA_DRV1_VMB_REGB;
    }else{
        pin_mask = (sup_num == 0) ? IO_TCA_DRV2_VMA_REGA : IO_TCA_DRV2_VMB_VSUP;
    }
    uint32_t pins_state = pin_mask;  
    tca_preset_pins(tca_dev_handle, pin_mask, pins_state, false);
    return ESP_OK;
}

esp_err_t io_sys_drv_reset_supply(bool drv_num){
    uint32_t pin_mask = (drv_num == 0) ? (IO_TCA_DRV1_VMA_VSUP | IO_TCA_DRV1_VMB_REGB) : (IO_TCA_DRV2_VMA_REGA | IO_TCA_DRV2_VMB_VSUP);
    tca_preset_pins(tca_dev_handle, pin_mask, 0UL, true);
    return ESP_OK;
}


esp_err_t io_sys_drv_callback_set(bool drv_num, void (*drv_callback)(void* arg), void* arg){
    uint32_t pin_mask = (drv_num == 0) ? IO_TCA_DRV1_FAULT : IO_TCA_DRV2_FAULT;
    tca_register_pin_callback(tca_dev_handle, __builtin_ctz(pin_mask), drv_callback, TCA_ON_CHANGE, arg);
    return ESP_OK;
}

esp_err_t io_sys_usb_callback_set(void (*usb_callback)(void* arg), void* arg){
    tca_register_pin_callback(tca_dev_handle, __builtin_ctz(IO_TCA_PD_INT), usb_callback, TCA_ON_CHANGE, arg);
    return ESP_OK;
}

esp_err_t io_sys_ina3221_callback_set(void (*ina_callback)(void* arg), void* arg){
    tca_register_pin_callback(tca_dev_handle, __builtin_ctz(IO_TCA_INA3221_WARN), ina_callback, TCA_ON_CHANGE, arg);
    tca_register_pin_callback(tca_dev_handle, __builtin_ctz(IO_TCA_INA3221_CRIT), ina_callback, TCA_ON_CHANGE, arg);
    return ESP_OK;
}

esp_err_t io_sys_callback_on_supply_type(void (*vsup_callback)(void* arg), void* arg){
    tca_register_pin_callback(tca_dev_handle, __builtin_ctz(IO_TCA_DRV1_VMA_VSUP), vsup_callback, TCA_ON_CHANGE, arg);
    tca_register_pin_callback(tca_dev_handle, __builtin_ctz(IO_TCA_DRV1_VMB_REGB), vsup_callback, TCA_ON_CHANGE, arg);
    tca_register_pin_callback(tca_dev_handle, __builtin_ctz(IO_TCA_DRV2_VMA_REGA), vsup_callback, TCA_ON_CHANGE, arg);
    tca_register_pin_callback(tca_dev_handle, __builtin_ctz(IO_TCA_DRV2_VMB_VSUP), vsup_callback, TCA_ON_CHANGE, arg);
    return ESP_OK;
}
