#include "rik_shared.h"
#include "manager_io.h"
#include "config_io.h"
#include "interface_dispatcher.h"
#include "interface_commands.h"
#include "rik_system_ctrl.h"

#define TAG __FILE_NAME__

#undef OWNER
#define OWNER OWNER_MANAGER_IO_PARSE_PACKET

#define CHECK_AND_RETURN(r) do { \
    status_rep_t _r = (r); \
    if (STA_IS_ERR(_r)) { \
        return STA_W(IO_ERR_PARSE_FAILED, OWNER, _r.e_code); \
    } \
    return _r; \
} while(0)


status_rep_t cfg_io_process_packet(const uint8_t* packet_data, uint16_t packet_len){
    status_rep_t r = STA_OK;
    switch(packet_data[0]){ 
        case CFG_IO_TYPE_GPIO_MODE: {
            cfg_io_gpio_mode_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_io_gpio_mode_t));
            r = SYS_GPIO_SET_MODE(settings.pin_id, settings.mode);
            CHECK_AND_RETURN(r);
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
                    .callback = rik_callback_adc, 
                    .arg = (void*)settings.pin_id 
            };
            r = SYS_IO_ADC_REGISTER_CALLBACK(settings.pin_id, &adc_cfg);
            ESP_LOGI(TAG, "Registering ADC alert for pin %d with thresholds [%d mV, %d mV] and hysteresis %d mV",
                     settings.pin_id, adc_cfg.adc_threshold_down_mv, adc_cfg.adc_threshold_up_mv, adc_cfg.adc_threshold_hysteresis_mv);
            CHECK_AND_RETURN(r);
        }
        case CFG_IO_TYPE_GPIO_INTERRUPT:{
            cfg_gpio_intr_mode_t settings;
            memcpy(&settings, packet_data +1, sizeof(cfg_gpio_intr_mode_t));
            r = SYS_GPIO_REGISTER_CALLBACK(settings.pin_id, settings.cfg_gpio_intr_mode, rik_callback_gpio, (void*)settings.pin_id);
            CHECK_AND_RETURN(r);
        }

        case CFG_IO_TYPE_GPIO_PWM_FREQ: {
            cfg_io_gpio_pwm_freq_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_io_gpio_pwm_freq_t));
            r = SYS_IO_SET_PWM_FREQ(settings.pin_id, settings.freq_hz);
            CHECK_AND_RETURN(r);
        }
        case CFG_IO_TYPE_GPIO_RESET: {
            cfg_io_gpio_reset_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_io_gpio_reset_t));
            r = SYS_GPIO_RESET_PIN(settings.pin_id);
            ESP_LOGI(TAG, "Resetting GPIO pin %d", settings.pin_id);
            CHECK_AND_RETURN(r);
        }
        
        default:
    }
    return STA_OK;
}
