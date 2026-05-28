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
                    .adc_threshold_up_mv = settings.cfg.adc_threshold_up_mv,
                    .adc_threshold_down_mv = settings.cfg.adc_threshold_down_mv,
                    .adc_threshold_hysteresis_mv = settings.cfg.adc_threshold_hysteresis_mv,
                    .adc_event_counter_threshold = settings.cfg.adc_event_counter_threshold,
                    .adc_window_mode = settings.cfg.adc_window_mode,
                    .callback = rik_callback_adc, // Placeholder callback
                    .arg = (void*)settings.pin_id // Placeholder argument
            };
            return SYS_IO_ADC_REGISTER_CALLBACK(settings.pin_id, &adc_cfg);
            return STA_OK;
        }
        case CFG_IO_TYPE_GPIO_PWM_FREQ: {
            cfg_io_gpio_pwm_freq_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_io_gpio_pwm_freq_t));
            return SYS_IO_SET_PWM_FREQ(settings.pin_id, settings.freq_hz);
            return STA_OK;
        }
        default:
    }
    return STA_OK;
}
