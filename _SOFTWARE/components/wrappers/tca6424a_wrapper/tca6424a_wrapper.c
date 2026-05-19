#include "tca6424a_wrapper.h"
#include "tca6424a.h"
#define TAG __FILE_NAME__

static tca6424a_handle_t tca_dev_handle = NULL;

static uint32_t outputs_mask = IO_TCA_PWM_nOE | IO_TCA_REGA_EN | IO_TCA_REGB_EN |
                            IO_TCA_DRV_OCPM | IO_TCA_DRV2_SLEEP | IO_TCA_DRV2_VMA_REGA |
                            IO_TCA_DRV2_VMB_VSUP | IO_TCA_DRV1_SLEEP | IO_TCA_DRV1_VMA_VSUP |
                            IO_TCA_DRV1_VMB_REGB | IO_TCA_LED_1 | IO_TCA_LED_2;
static uint32_t inputs_mask = IO_TCA_REGA_INT | IO_TCA_REGB_INT | IO_TCA_VUSB_OK |
                           IO_TCA_VEXT_OK | IO_TCA_INA3221_WARN | IO_TCA_INA3221_CRIT |
                           IO_TCA_DRV2_FAULT | IO_TCA_DRV1_FAULT | IO_TCA_PD_INT;

status_rep_t sys_io_init_expander(void* dev) {
    tca_dev_handle = dev;
    return STA_FROM_ESP(tca_preset_cfg(dev, inputs_mask | outputs_mask, inputs_mask, true), OWNER_TCA6424A_WRAPPER_INIT, 0);
}

status_rep_t sys_io_set_led(bool led_num, bool state){
    uint32_t pin_mask = (led_num == 0) ? IO_TCA_LED_1 : IO_TCA_LED_2;
    uint32_t pins_state = state ? pin_mask : 0;  
    return STA_FROM_ESP(tca_preset_pins(tca_dev_handle, pin_mask, pins_state, false), OWNER_TCA6424A_WRAPPER_SET_PINS, __builtin_ctz(pin_mask));
    
}

status_rep_t sys_io_expander_set_pin(uint32_t pin_mask, bool state) {
    // Check if the requested pin_mask has any bits outside of the defined outputs_mask
    if (pin_mask & ~outputs_mask) {
        // Isolate the invalid bits to report exactly which wrong pin was requested
        uint32_t invalid_pins = pin_mask & ~outputs_mask;
        return STA_C(0, OWNER_TCA6424A_WRAPPER_SET_PINS, __builtin_ctz(invalid_pins));
    }
    
    uint32_t pins_state = state ? pin_mask : 0;  
    return STA_FROM_ESP(tca_preset_pins(tca_dev_handle, pin_mask, pins_state, false), OWNER_TCA6424A_WRAPPER_SET_PINS, __builtin_ctz(pin_mask));
}


void sys_io_expander_set_pin_callback(uint32_t pin_mask, uint32_t mode, void (*callback)(void* arg), void* arg){
    //tca_register_pin_callback(tca_dev_handle, pin_mask, callback, mode, arg);
}

extern void tca_isr_callback(void* arg);

// status_rep_t io_sys_periph_en_pca9685(bool state){
//     return STA_FROM_ESP(tca_preset_pins(tca_dev_handle, IO_TCA_PWM_nOE, state ? IO_TCA_PWM_nOE : 0UL, false), OWNER_TCA6424A_WRAPPER_SET_PINS, __builtin_ctz(IO_TCA_PWM_nOE));
// }

// status_rep_t io_sys_preriph_en_tps55289(bool vreg_num, bool state){
//     uint32_t pin_mask = (vreg_num == 0) ? IO_TCA_REGA_EN : IO_TCA_REGB_EN;
//     uint32_t pins_state = state ? pin_mask : 0;  
//     return STA_FROM_ESP(tca_preset_pins(tca_dev_handle, pin_mask, pins_state, false), OWNER_TCA6424A_WRAPPER_SET_PINS, __builtin_ctz(pin_mask));
// }   


// status_rep_t io_sys_periph_set_drv_ocpm(bool mode){
//     return STA_FROM_ESP(tca_preset_pins(tca_dev_handle, IO_TCA_DRV_OCPM, mode ? IO_TCA_DRV_OCPM : 0UL, false), OWNER_TCA6424A_WRAPPER_SET_PINS, __builtin_ctz(IO_TCA_DRV_OCPM));
// }

// status_rep_t io_sys_periph_en_drv(bool drv_num, bool state){
//     uint32_t pin_mask = (drv_num == 0) ? IO_TCA_DRV1_SLEEP : IO_TCA_DRV2_SLEEP;
//     uint32_t pins_state = state ? pin_mask : 0;  
//     return STA_FROM_ESP(tca_preset_pins(tca_dev_handle, pin_mask, pins_state, false), OWNER_TCA6424A_WRAPPER_SET_PINS, __builtin_ctz(pin_mask));
// }

// status_rep_t io_sys_periph_set_drv_supply(bool drv_num, bool sup_num){
//     uint32_t pin_mask;
//     if(drv_num == 0){
//         pin_mask = (sup_num == 0) ? IO_TCA_DRV1_VMA_VSUP : IO_TCA_DRV1_VMB_REGB;
//     }else{
//         pin_mask = (sup_num == 0) ? IO_TCA_DRV2_VMA_REGA : IO_TCA_DRV2_VMB_VSUP;
//     }
//     uint32_t pins_state = pin_mask;  
//     return STA_FROM_ESP(tca_preset_pins(tca_dev_handle, pin_mask, pins_state, true), OWNER_TCA6424A_WRAPPER_SET_PINS, __builtin_ctz(pin_mask)); 
// }

// status_rep_t io_sys_periph_reset_drv_supply(bool drv_num){
//     uint32_t pin_mask = (drv_num == 0) ? (IO_TCA_DRV1_VMA_VSUP | IO_TCA_DRV1_VMB_REGB) : (IO_TCA_DRV2_VMA_REGA | IO_TCA_DRV2_VMB_VSUP);
//     return STA_FROM_ESP(tca_preset_pins(tca_dev_handle, pin_mask, 0UL, true), OWNER_TCA6424A_WRAPPER_SET_PINS, __builtin_ctz(pin_mask));
   
// }


// status_rep_t io_sys_callback_set_drv(bool drv_num, void (*drv_callback)(void* arg), void* arg){
//     uint32_t pin_mask = (drv_num == 0) ? IO_TCA_DRV1_FAULT : IO_TCA_DRV2_FAULT;
//     return STA_FROM_ESP(tca_register_pin_callback(tca_dev_handle, pin_mask, drv_callback, TCA_ON_FALLING_EDGE, arg), OWNER_TCA6424A_WRAPPER_SET_PINS, __builtin_ctz(pin_mask));
// }

// status_rep_t io_sys_callback_set_usb(void (*usb_callback)(void* arg), void* arg){
//     return STA_FROM_ESP(tca_register_pin_callback(tca_dev_handle, IO_TCA_PD_INT, usb_callback, TCA_ON_FALLING_EDGE, arg), OWNER_TCA6424A_WRAPPER_SET_PINS, __builtin_ctz(IO_TCA_PD_INT));
// }

// status_rep_t io_sys_callback_set_ina3221(void (*ina_callback_c)(void* arg), void* arg_c, void (*ina_callback_w)(void* arg), void* arg_w){
//     esp_err_t err = tca_register_pin_callback(tca_dev_handle, IO_TCA_INA3221_WARN, ina_callback_w, TCA_ON_FALLING_EDGE, arg_w);
//     if(err == ESP_OK){
//         err = tca_register_pin_callback(tca_dev_handle, IO_TCA_INA3221_CRIT, ina_callback_c, TCA_ON_FALLING_EDGE, arg_c);
//     }
//     ESP_LOGI(TAG, "Registered INA3221 callbacks for warning and critical alerts");
//     return STA_FROM_ESP(err, OWNER_TCA6424A_WRAPPER_SET_PINS, 0);
// }

// status_rep_t io_sys_callback_set_tps55289(bool vreg_num, void (*tps_callback)(void* arg), void* arg){
//     uint32_t pin_mask = (vreg_num == 0) ? IO_TCA_REGA_INT : IO_TCA_REGB_INT;
//     esp_err_t err = tca_register_pin_callback(tca_dev_handle, pin_mask, tps_callback, TCA_ON_FALLING_EDGE, arg);
//     if (err == ESP_OK) {
//         ESP_LOGI(TAG, "Registered TPS55289 callback for fault alerts");
//     }
//     return STA_FROM_ESP(err, OWNER_TCA6424A_WRAPPER_SET_PINS, __builtin_ctz(pin_mask));
// }


