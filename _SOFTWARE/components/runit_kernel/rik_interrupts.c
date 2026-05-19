#include "rik_interrupts.h"
#include "tca6424a_wrapper.h"
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
    STA_RET_ON_ESP_ERR(gpio_isr_handler_add(IO_SYS_PIN_TCA6424_nINT, &tca_isr_callback, m_i2c_get_dev_handle(rik_tca_id)), OWNER_RIK_INTERRUPTS_CONFIG, IO_SYS_PIN_TCA6424_nINT);
    STA_RET_ON_ESP_ERR(gpio_isr_handler_add(IO_SYS_PIN_ADS_ALERT, NULL, NULL), OWNER_RIK_INTERRUPTS_CONFIG, IO_SYS_PIN_ADS_ALERT);
    return STA_OK;
}

