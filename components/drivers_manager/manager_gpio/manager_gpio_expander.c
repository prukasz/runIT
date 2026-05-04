#include "manager_gpio_expander.h"

static void * tca_dev_handle = NULL;

esp_err_t io_sys_led_set(bool led_num, bool state){
    uint32_t pin_mask = (led_num == 0) ? IO_TCA_LED_1 : IO_TCA_LED_2;
    uint32_t pins_state = state ? pin_mask : 0;  
    tca_set_pins(tca_dev_handle, pins_state, pin_mask);
    return ESP_OK;
}       

esp_err_t io_sys_pwm_en(bool state){
    tca_set_pins(tca_dev_handle, IO_TCA_PWM_nOE, state ? IO_TCA_PWM_nOE : 0UL);
    return ESP_OK;
}

esp_err_t io_sys_vreg_en(bool vreg_num, bool state){
    uint32_t pin_mask = (vreg_num == 0) ? IO_TCA_REGA_EN : IO_TCA_REGB_EN;
    uint32_t pins_state = state ? pin_mask : 0;  
    tca_set_pins(tca_dev_handle, pins_state, pin_mask);
    return ESP_OK;
}

esp_err_t io_sys_drv_ocpm(bool mode){
    tca_set_pins(tca_dev_handle, IO_TCA_DRV_OCPM, mode ? IO_TCA_DRV_OCPM : 0UL);
    return ESP_OK;
}

esp_err_t io_sys_drv_sleep(bool drv_num, bool state){
    uint32_t pin_mask = (drv_num == 0) ? IO_TCA_DRV1_SLEEP : IO_TCA_DRV2_SLEEP;
    uint32_t pins_state = state ? pin_mask : 0;  
    tca_set_pins(tca_dev_handle, pins_state, pin_mask);
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
    tca_set_pins(tca_dev_handle, pins_state, pin_mask);
    return ESP_OK;
}

esp_err_t io_sys_drv_reset_supply(bool drv_num){
    uint32_t pin_mask = (drv_num == 0) ? (IO_TCA_DRV1_VMA_VSUP | IO_TCA_DRV1_VMB_REGB) : (IO_TCA_DRV2_VMA_REGA | IO_TCA_DRV2_VMB_VSUP);
    tca_set_pins(tca_dev_handle, pin_mask, 0UL );
    return ESP_OK;
}


esp_err_t io_sys_drv_callback_set(bool drv_num, void (*drv_callback)(uint32_t pins)){
    uint32_t pin_mask = (drv_num == 0) ? IO_TCA_DRV1_FAULT : IO_TCA_DRV2_FAULT;
    tca_set_int_callback(tca_dev_handle, drv_callback, pin_mask);
    return ESP_OK;
}

esp_err_t io_sys_usb_callback_set(void (*usb_callback)(uint32_t pins)){
    tca_set_int_callback(tca_dev_handle, usb_callback, IO_TCA_PD_INT);
    return ESP_OK;
}

esp_err_t io_sys_ina3221_callback_set(void (*ina_callback)(uint32_t pins)){
    tca_set_int_callback(tca_dev_handle, ina_callback, IO_TCA_INA3221_WARN | IO_TCA_INA3221_CRIT);
    return ESP_OK;
}

esp_err_t io_sys_callback_on_supply_type(void (*vsup_callback)(uint32_t pins)){
    tca_set_int_callback(tca_dev_handle, vsup_callback, IO_TCA_DRV1_VMA_VSUP | IO_TCA_DRV1_VMB_REGB | IO_TCA_DRV2_VMA_REGA | IO_TCA_DRV2_VMB_VSUP); // arbitrary sum as example
    return ESP_OK;
}