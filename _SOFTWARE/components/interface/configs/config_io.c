#include "rik_shared.h"
#include "manager_io.h"
#include "config_io.h"
#include "interface_dispatcher.h"
#include "interface_commands.h"
#include "rik_system_ctrl.h"

#define TAG __FILE_NAME__


status_rep_t cfg_io_process_packet(const uint8_t* packet_data, uint16_t packet_len){
    switch(packet_data[0]){ 
        case CFG_IO_TYPE_GPIO_MODE: {
            cfg_io_gpio_mode_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_io_gpio_mode_t));
            return SYS_GPIO_SET_MODE(settings.pin_id, settings.mode);
        }
        case CFG_IO_TYPE_GPIO_ADC_ALERT: {
            cfg_io_gpio_adc_alert_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_io_gpio_adc_alert_t));
            sys_io_adc_int_config_t adc_cfg = {
                    .adc_threshold_up_mv = settings.adc_threshold_up_mv,
                    .adc_threshold_down_mv = settings.adc_threshold_down_mv,
                    .adc_threshold_hysteresis_mv = settings.adc_threshold_hysteresis_mv,
                    .adc_event_counter_threshold = settings.adc_event_counter_threshold,
                    .adc_window_mode = settings.adc_window_mode,
                    .callback = rik_callback_adc, // Placeholder callback
                    .arg = (void*)settings.pin_id // Placeholder argument
            };
            SYS_IO_ADC_REGISTER_CALLBACK(settings.pin_id, &adc_cfg);
            ESP_LOGI(TAG, "Registered ADC alert for pin %d with thresholds [%d mV, %d mV] and hysteresis %d mV",
                     settings.pin_id, adc_cfg.adc_threshold_down_mv, adc_cfg.adc_threshold_up_mv, adc_cfg.adc_threshold_hysteresis_mv);
            return STA_OK;
        }
        case CFG_IO_TYPE_GPIO_PWM_FREQ: {
            cfg_io_gpio_pwm_freq_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_io_gpio_pwm_freq_t));
            return SYS_IO_SET_PWM_FREQ(settings.pin_id, settings.freq_hz);
            return STA_OK;
        }
        case CFG_IO_TYPE_GPIO_RESET: {
            cfg_io_gpio_reset_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_io_gpio_reset_t));
            STA_RET_ON_ERR(SYS_GPIO_RESET_PIN(settings.pin_id));
            ESP_LOGI(TAG, "Reset GPIO pin %d", settings.pin_id);
            return STA_OK;
        }
        default:
    }
    return STA_OK;
}
