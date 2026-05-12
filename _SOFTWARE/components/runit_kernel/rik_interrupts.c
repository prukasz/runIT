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

/* Int interupts and callbacks on esp32 chip directly */
status_rep_t rik_init_intr_esp(void){
    STA_RET_ON_ESP_ERR(gpio_install_isr_service(0), OWNER_RIK_INTERRUPTS_CONFIG, 0);
    STA_RET_ON_ESP_ERR(gpio_config(&_gpio_cfg_9), OWNER_RIK_INTERRUPTS_CONFIG, 9);
    STA_RET_ON_ESP_ERR(gpio_config(&_gpio_cfg_42), OWNER_RIK_INTERRUPTS_CONFIG, 42);
    STA_RET_ON_ESP_ERR(gpio_isr_handler_add(IO_SYS_PIN_TCA6424_nINT, tca_isr_callback, m_i2c_get_dev_handle(rik_tca_id)), OWNER_RIK_INTERRUPTS_CONFIG, IO_SYS_PIN_TCA6424_nINT);
    STA_RET_ON_ESP_ERR(gpio_isr_handler_add(IO_SYS_PIN_ADS_ALERT, NULL, NULL), OWNER_RIK_INTERRUPTS_CONFIG, IO_SYS_PIN_ADS_ALERT);
    tca_mock_set_intr_callback(tca_isr_callback, m_i2c_get_dev_handle(rik_tca_id));

    return STA_OK;
}

/*Bind specific callbacks to specific pin on tca6424a*/
status_rep_t rik_init_pins_callbacks_tca6424a(void){

    /*ina->tca_pins->ina fault callbacks*/
    io_sys_callback_set_ina3221(ina3221_isr_callback_critical, m_i2c_get_dev_handle(rik_ina_id), ina3221_isr_callback_warning, m_i2c_get_dev_handle(rik_ina_id));
   
    /*drv->tca_pin->drv fault callbacks*/
    /*add callbacks when driver is completed so react to errors on drvs*/
    io_sys_callback_set_drv(0, NULL, NULL);
    io_sys_callback_set_drv(1, NULL, NULL);

    /*tps->tca_pin->tps fault callbacks*/
    io_sys_callback_set_tps55289(0, tps55289_isr_callback_fault, m_i2c_get_dev_handle(rik_tps_0_id)); 
    io_sys_callback_set_tps55289(1, tps55289_isr_callback_fault, m_i2c_get_dev_handle(rik_tps_1_id)); 

    //mock callbacks
    #ifdef CONNECT_TPS55289_INT_MOCK
    tps_mock_set_intr_callback(0x74,tps55289_isr_callback_fault, m_i2c_get_dev_handle(rik_tps_0_id));
    tps_mock_set_intr_callback(0x75,tps55289_isr_callback_fault, m_i2c_get_dev_handle(rik_tps_1_id));
    #endif
    // io_sys_callback_set_usb(NULL, NULL);
    ESP_LOGI(TAG, "Initialized TCA6424A interrupts");
    return STA_OK;
}

void dummy_int(void* arg){
    ESP_LOGI(TAG, "Dummy callback activated");\
    tps55289_set_current_limit(arg, true, 100);
};

void rik_init_usr_callbacks_tps55289(void){

    tps55289_register_user_callback(m_i2c_get_dev_handle(rik_tps_0_id), TPS55289_FAULT_OVP, &dummy_int, m_i2c_get_dev_handle(rik_tps_0_id));
    tps55289_register_user_callback(m_i2c_get_dev_handle(rik_tps_1_id), TPS55289_FAULT_OVP, &dummy_int, m_i2c_get_dev_handle(rik_tps_1_id));
    tps55289_register_user_callback(m_i2c_get_dev_handle(rik_tps_0_id), TPS55289_FAULT_OCP, &dummy_int, m_i2c_get_dev_handle(rik_tps_0_id));
    tps55289_register_user_callback(m_i2c_get_dev_handle(rik_tps_1_id), TPS55289_FAULT_OCP, &dummy_int, m_i2c_get_dev_handle(rik_tps_1_id));
    tps55289_register_user_callback(m_i2c_get_dev_handle(rik_tps_0_id), TPS55289_FAULT_SCP, &dummy_int, m_i2c_get_dev_handle(rik_tps_0_id));
    tps55289_register_user_callback(m_i2c_get_dev_handle(rik_tps_1_id), TPS55289_FAULT_SCP, &dummy_int, m_i2c_get_dev_handle(rik_tps_1_id));

    ESP_LOGI(TAG, "Initialized TPS55289 user callbacks");
}
