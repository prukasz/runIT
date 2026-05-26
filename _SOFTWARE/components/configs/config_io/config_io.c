#include "rik_shared.h"
#include "manager_io.h"
#include "config_io.h"
#include "interface_dispatcher.h"
#include "interface_commands.h"

#define TAG __FILE_NAME__

void test_callback(void* arg){
    ESP_LOGI(TAG, "user test ADC callback invoked with arg: %p", arg);
}

status_rep_t cfg_io_process_packet(const uint8_t* packet_data, uint16_t packet_len){
    switch(packet_data[0]){ // Assuming first byte is packet type
        case CFG_IO_TYPE_GPIO_MODE: {
            cfg_io_gpio_mode_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_io_gpio_mode_t));
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
                    .callback = test_callback, // Placeholder callback
                    .arg = NULL // Placeholder argument
            };
            adc_cfg.arg = (void*)SYS_IO_GET_PIN(settings.pin_id);// Pass pin_id as argument to callback
            return SYS_IO_ADC_REGISTER_CALLBACK(settings.pin_id, &adc_cfg);
            return STA_OK;
        }
        case CFG_IO_TYPE_GPIO_PWM_FREQ: {
            ESP_LOGW(TAG, "Received GPIO PWM frequency config packet - PWM frequency configuration not implemented yet");
            return STA_OK;
        }
        default:
    }
    return STA_OK;
}

status_rep_t cfg_io_init(void){
    return interface_register_parser(PACKET_H_CFG_IO, cfg_io_process_packet);
}