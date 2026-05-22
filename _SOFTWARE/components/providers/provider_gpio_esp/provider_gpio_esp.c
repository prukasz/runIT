#include "esp_adc_config.h"
#include "provider_gpio_esp.h"

#define TAG __FILE_NAME__   



static uint64_t aviable_adc_pins_mask = 0;
static uint64_t aviable_pwm_pins_mask = 0;

// SYS_GPIO_MODE_INPUT = 0,
//     SYS_GPIO_MODE_OUTPUT_PUSH_PULL = 1,
//     SYS_GPIO_MODE_OUTPUT_OPEN_DRAIN = 2,
//     SYS_GPIO_MODE_INPUT_PULLUP = 3,
//     SYS_GPIO_MODE_INPUT_PULLDOWN = 4,
//     SYS_GPIO_MODE_PWM = 5,
//     SYS_GPIO_MODE_ADC = 6

static uint64_t congigured_modes_mask[7] = {0}; 



status_rep_t provider_gpio_esp_set_pin_mode(uint64_t pin_mask, uint32_t mode){
    // for(int i = 0; i < 7; i++)
    //     if (mode == i){ 
    //     congigured_modes_mask[i] |= pin_mask;
    //     }
        return STA_OK;
}

status_rep_t provider_gpio_esp_set_level(uint64_t pin_mask, bool level);

status_rep_t provider_gpio_esp_read_level(uint64_t pin_mask, bool* level);

status_rep_t provider_gpio_esp_toggle(uint64_t pin_mask);

status_rep_t provider_gpio_esp_register_callback(uint64_t pin_mask, uint32_t mode, void (*callback)(void* arg), void* arg);

status_rep_t provider_gpio_esp_set_pwm_duty(uint64_t pin_mask, uint32_t duty_cycle);

status_rep_t provider_gpio_esp_set_pwm_freq(uint64_t pin_mask, uint32_t freq_hz);

status_rep_t provider_gpio_esp_adc_read(uint64_t pin_mask, uint32_t* out_mv, uint8_t max_results_num);

status_rep_t provider_gpio_esp_adc_register_callback(uint64_t pin_mask, void* adc_int_config);

status_rep_t provider_gpio_esp_suppress_updates(bool suppress);