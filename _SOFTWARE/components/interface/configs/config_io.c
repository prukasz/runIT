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
        case CFG_IO_TYPE_GPIO_SET_LEVEL: {
            cfg_io_gpio_set_level_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_io_gpio_set_level_t));
            r = SYS_GPIO_SET_LEVEL(settings.pin_id, settings.level);
            ESP_LOGI(TAG, "SYS_GPIO_SET_LEVEL pin_id=%lu, level=%d", (unsigned long)settings.pin_id, settings.level);
            CHECK_AND_RETURN(r);
        }
        case CFG_IO_TYPE_GPIO_GET_LEVEL: {
            cfg_io_gpio_get_level_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_io_gpio_get_level_t));
            int val = 0;
            r = SYS_GPIO_READ_LEVEL(settings.pin_id, &val);
            ESP_LOGI(TAG, "SYS_GPIO_READ_LEVEL pin_id=%lu -> %d", (unsigned long)settings.pin_id, val);
            CHECK_AND_RETURN(r);
        }
        case CFG_IO_TYPE_GPIO_TOGGLE: {
            cfg_io_gpio_toggle_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_io_gpio_toggle_t));
            r = SYS_GPIO_TOGGLE(settings.pin_id);
            ESP_LOGI(TAG, "SYS_GPIO_TOGGLE pin_id=%lu", (unsigned long)settings.pin_id);
            CHECK_AND_RETURN(r);
        }
        case CFG_IO_TYPE_ADC_READ_MV: {
            cfg_io_adc_read_mv_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_io_adc_read_mv_t));
            uint32_t val = 0;
            r = SYS_IO_ADC_READ(settings.pin_id, &val);
            ESP_LOGI(TAG, "SYS_IO_ADC_READ pin_id=%lu -> %lu mV", (unsigned long)settings.pin_id, (unsigned long)val);
            CHECK_AND_RETURN(r);
        }
        case CFG_IO_TYPE_GPIO_PWM_DUTY: {
            cfg_io_gpio_pwm_duty_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_io_gpio_pwm_duty_t));
            r = SYS_IO_SET_PWM_DUTY(settings.pin_id, settings.duty_cycle);
            ESP_LOGI(TAG, "SYS_IO_SET_PWM_DUTY pin_id=%llu, duty=%lu", (unsigned long long)settings.pin_id, (unsigned long)settings.duty_cycle);
            CHECK_AND_RETURN(r);
        }
        case CFG_IO_TYPE_RESET_ALL: {
            r = sys_io_reset_all();
            ESP_LOGI(TAG, "SYS_IO_RESET_ALL executed");
            CHECK_AND_RETURN(r);
        }
        
        default:
    }
    return STA_OK;
}
