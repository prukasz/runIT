#include "rik_interrupts.h"
#include "tca6424a_wrapper.h"
#include "ina3221_wrapper.h"
#include "tca6424a_mock.h"
#include "tps55289.h"
#include "tps55289_mock.h"
#include "manager_i2c.h"
#include "rik_shared.h"

#define TAG __FILE_NAME__


gpio_config_t _gpio_cfg_9 = {
    .pin_bit_mask = (1ULL << IO_SYS_PIN_TCA6424_nINT),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_NEGEDGE
};

gpio_config_t _gpio_cfg_42 = {
    .pin_bit_mask = (1ULL << IO_SYS_PIN_ADS_ALERT),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_NEGEDGE
};

status_rep_t rik_init_intr_esp(void){
    esp_err_t err;
    err = gpio_install_isr_service(0);
    if (err != ESP_OK) {
        return STA_C(err, OWN_rik_init_intr_esp, 0);
    }
    err = gpio_config(&_gpio_cfg_9);
    if (err != ESP_OK) {
        return STA_C(err, OWN_rik_init_intr_esp, 9);
    }
    err = gpio_config(&_gpio_cfg_42);
    if (err != ESP_OK) {
        return STA_C(err, OWN_rik_init_intr_esp, 42);
    }
    gpio_isr_handler_add(IO_SYS_PIN_TCA6424_nINT, tca_isr_callback, m_i2c_get_dev_handle(rik_tca_id));
    tca_mock_set_intr_callback(tca_isr_callback, m_i2c_get_dev_handle(rik_tca_id));
    /*add when driver completed*/
    //gpio_isr_handler_add(IO_SYS_PIN_ADS_ALERT, NULL, NULL);
    return STA_OK;
}

status_rep_t rik_init_intr_tca6424a(void){
    io_sys_callback_set_ina3221(ina3221_isr_callback_critical, m_i2c_get_dev_handle(rik_ina_id), ina3221_isr_callback_warning, m_i2c_get_dev_handle(rik_ina_id));
    /*enable and fill when driver completed*/
    io_sys_callback_set_drv(0, NULL, NULL);
    io_sys_callback_set_drv(1, NULL, NULL);
    io_sys_callback_set_tps55289(0, tps55289_isr_callback_fault, m_i2c_get_dev_handle(rik_tps_0_id)); 
    io_sys_callback_set_tps55289(1, tps55289_isr_callback_fault, m_i2c_get_dev_handle(rik_tps_1_id)); 

    //mock callbacks
    tps_mock_set_intr_callback(0x74,tps55289_isr_callback_fault, m_i2c_get_dev_handle(rik_tps_0_id));
    tps_mock_set_intr_callback(0x75,tps55289_isr_callback_fault, m_i2c_get_dev_handle(rik_tps_1_id));
    // io_sys_callback_set_usb(NULL, NULL);
    ESP_LOGI(TAG, "Initialized TCA6424A interrupts");
    return STA_OK;
}

void dummy_int(void* arg){

    ESP_LOGI(TAG, "Dummy callback activated");\
    tps55289_set_current_limit(arg, true, 100);
};

status_rep_t rik_init_intr_tps55289(void){

    tps55289_register_user_callback(m_i2c_get_dev_handle(rik_tps_0_id), TPS55289_FAULT_OVP, &dummy_int, m_i2c_get_dev_handle(rik_tps_0_id));
    tps55289_register_user_callback(m_i2c_get_dev_handle(rik_tps_1_id), TPS55289_FAULT_OVP, &dummy_int, m_i2c_get_dev_handle(rik_tps_1_id));
    tps55289_register_user_callback(m_i2c_get_dev_handle(rik_tps_0_id), TPS55289_FAULT_OCP, &dummy_int, m_i2c_get_dev_handle(rik_tps_0_id));
    tps55289_register_user_callback(m_i2c_get_dev_handle(rik_tps_1_id), TPS55289_FAULT_OCP, &dummy_int, m_i2c_get_dev_handle(rik_tps_1_id));
    tps55289_register_user_callback(m_i2c_get_dev_handle(rik_tps_0_id), TPS55289_FAULT_SCP, &dummy_int, m_i2c_get_dev_handle(rik_tps_0_id));
    tps55289_register_user_callback(m_i2c_get_dev_handle(rik_tps_1_id), TPS55289_FAULT_SCP, &dummy_int, m_i2c_get_dev_handle(rik_tps_1_id));

    ESP_LOGI(TAG, "Initialized TPS55289 user interrupts");
    return STA_OK;
}
