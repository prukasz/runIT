#include "tca6424a_wrapper.h"
#include "esp_log.h"
#define TAG __FILE_NAME__

static void * tca_dev_handle = NULL;


esp_err_t io_sys_tca6424a_init(void* dev) {

    tca_dev_handle = dev;

    uint32_t outputs_mask = IO_TCA_PWM_nOE | IO_TCA_REGA_EN | IO_TCA_REGB_EN |
                            IO_TCA_DRV_OCPM | IO_TCA_DRV2_SLEEP | IO_TCA_DRV2_VMA_REGA |
                            IO_TCA_DRV2_VMB_VSUP | IO_TCA_DRV1_SLEEP | IO_TCA_DRV1_VMA_VSUP |
                            IO_TCA_DRV1_VMB_REGB | IO_TCA_LED_1 | IO_TCA_LED_2;

    uint32_t inputs_mask = IO_TCA_REGA_INT | IO_TCA_REGB_INT | IO_TCA_VUSB_OK |
                           IO_TCA_VEXT_OK | IO_TCA_INA3221_WARN | IO_TCA_INA3221_CRIT |
                           IO_TCA_DRV2_FAULT | IO_TCA_DRV1_FAULT | IO_TCA_PD_INT;

    return tca_preset_cfg(dev, inputs_mask | outputs_mask, inputs_mask, true);
}

esp_err_t io_sys_periph_set_led(bool led_num, bool state){
    uint32_t pin_mask = (led_num == 0) ? IO_TCA_LED_1 : IO_TCA_LED_2;
    uint32_t pins_state = state ? pin_mask : 0;  
    tca_preset_pins(tca_dev_handle, pin_mask, pins_state, false);
    return ESP_OK;
}       

esp_err_t io_sys_periph_en_pca9685(bool state){
    tca_preset_pins(tca_dev_handle, IO_TCA_PWM_nOE, state ? IO_TCA_PWM_nOE : 0UL, false);
    return ESP_OK;
}

esp_err_t io_sys_preriph_en_tps55289(bool vreg_num, bool state){
    uint32_t pin_mask = (vreg_num == 0) ? IO_TCA_REGA_EN : IO_TCA_REGB_EN;
    uint32_t pins_state = state ? pin_mask : 0;  
    tca_preset_pins(tca_dev_handle, pin_mask, pins_state, false);
    return ESP_OK;
}

esp_err_t io_sys_periph_set_drv_ocpm(bool mode){
    tca_preset_pins(tca_dev_handle, IO_TCA_DRV_OCPM, mode ? IO_TCA_DRV_OCPM : 0UL, false);
    return ESP_OK;
}

esp_err_t io_sys_periph_en_drv(bool drv_num, bool state){
    uint32_t pin_mask = (drv_num == 0) ? IO_TCA_DRV1_SLEEP : IO_TCA_DRV2_SLEEP;
    uint32_t pins_state = state ? pin_mask : 0;  
    tca_preset_pins(tca_dev_handle, pin_mask, pins_state, false);
    return ESP_OK;
}

esp_err_t io_sys_periph_set_drv_supply(bool drv_num, bool sup_num){
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

esp_err_t io_sys_periph_reset_drv_supply(bool drv_num){
    uint32_t pin_mask = (drv_num == 0) ? (IO_TCA_DRV1_VMA_VSUP | IO_TCA_DRV1_VMB_REGB) : (IO_TCA_DRV2_VMA_REGA | IO_TCA_DRV2_VMB_VSUP);
    tca_preset_pins(tca_dev_handle, pin_mask, 0UL, true);
    return ESP_OK;
}


esp_err_t io_sys_callback_set_drv(bool drv_num, void (*drv_callback)(void* arg), void* arg){
    uint32_t pin_mask = (drv_num == 0) ? IO_TCA_DRV1_FAULT : IO_TCA_DRV2_FAULT;
    tca_register_pin_callback(tca_dev_handle, pin_mask, drv_callback, TCA_ON_FALLING_EDGE, arg);
    return ESP_OK;
}

esp_err_t io_sys_callback_set_usb(void (*usb_callback)(void* arg), void* arg){
    tca_register_pin_callback(tca_dev_handle, IO_TCA_PD_INT, usb_callback, TCA_ON_FALLING_EDGE, arg);
    return ESP_OK;
}

esp_err_t io_sys_callback_set_ina3221(void (*ina_callback_c)(void* arg), void* arg_c, void (*ina_callback_w)(void* arg), void* arg_w){
    tca_register_pin_callback(tca_dev_handle, IO_TCA_INA3221_WARN, ina_callback_w, TCA_ON_FALLING_EDGE, arg_w);
    tca_register_pin_callback(tca_dev_handle, IO_TCA_INA3221_CRIT, ina_callback_c, TCA_ON_FALLING_EDGE, arg_c);
    ESP_LOGI(TAG, "Registered INA3221 callbacks for warning and critical alerts");
    return ESP_OK;
}

esp_err_t io_sys_callback_set_tps55289(bool vreg_num, void (*tps_callback)(void* arg), void* arg){
    uint32_t pin_mask = (vreg_num == 0) ? IO_TCA_REGA_INT : IO_TCA_REGB_INT;
    tca_register_pin_callback(tca_dev_handle, pin_mask, tps_callback, TCA_ON_FALLING_EDGE, arg);
    ESP_LOGI(TAG, "Registered TPS55289 callback for fault alerts");
    return ESP_OK;
}


