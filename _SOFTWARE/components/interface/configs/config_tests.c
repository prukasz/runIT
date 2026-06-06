#include "config_tests.h"
#include "manager_io.h"
#include "manager_power.h"
#include "rik_shared.h"
#include "esp_log.h"
#include <string.h>

#define TAG "CONFIG_TESTS"

status_rep_t cfg_tests_process_packet(const uint8_t* packet_data, uint16_t packet_len){
    switch(packet_data[0]){
        case CFG_TEST_TYPE_GPIO_SET_LEVEL: {
            cfg_test_gpio_set_level_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_test_gpio_set_level_t));
            SYS_GPIO_SET_LEVEL(settings.pin_id, settings.level);
            ESP_LOGI(TAG, "SYS_GPIO_SET_LEVEL pin_id=%lu, level=%d", (unsigned long)settings.pin_id, settings.level);
            break;
        }
        case CFG_TEST_TYPE_GPIO_GET_LEVEL: {
            cfg_test_gpio_get_level_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_test_gpio_get_level_t));
            int val = 0;
            SYS_GPIO_READ_LEVEL(settings.pin_id, &val);
            ESP_LOGI(TAG, "SYS_GPIO_READ_LEVEL pin_id=%lu -> %d", (unsigned long)settings.pin_id, val);
            break;
        }
        case CFG_TEST_TYPE_GPIO_TOGGLE: {
            cfg_test_gpio_toggle_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_test_gpio_toggle_t));
            SYS_GPIO_TOGGLE(settings.pin_id);
            ESP_LOGI(TAG, "SYS_GPIO_TOGGLE pin_id=%lu", (unsigned long)settings.pin_id);
            break;
        }
        case CFG_TEST_TYPE_ADC_READ_MV: {
            cfg_test_adc_read_mv_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_test_adc_read_mv_t));
            uint32_t val = 0;
            SYS_IO_ADC_READ(settings.pin_id, &val);
            ESP_LOGI(TAG, "SYS_IO_ADC_READ pin_id=%lu -> %lu mV", (unsigned long)settings.pin_id, (unsigned long)val);
            break;
        }
        case CFG_TEST_TYPE_GPIO_PWM_DUTY: {
            cfg_test_gpio_pwm_duty_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_test_gpio_pwm_duty_t));
            SYS_IO_SET_PWM_DUTY(settings.pin_id, settings.duty_cycle);
            ESP_LOGI(TAG, "SYS_IO_SET_PWM_DUTY pin_id=%llu, duty=%lu", (unsigned long long)settings.pin_id, (unsigned long)settings.duty_cycle);
            break;
        }
        case CFG_TEST_TYPE_RESET_ALL: {
            sys_io_reset_all();
            ESP_LOGI(TAG, "SYS_IO_RESET_ALL executed");
            break;
        }
        case CFG_TEST_TYPE_GET_REG_VOLTAGE: {
            cfg_test_get_reg_voltage_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_test_get_reg_voltage_t));
            uint32_t val = 0;
            sys_pwr_get_bus_voltage(settings.regulator_num, &val);
            ESP_LOGI(TAG, "sys_pwr_get_bus_voltage reg=%d -> %lu mV", settings.regulator_num, (unsigned long)val);
            break;
        }
        case CFG_TEST_TYPE_GET_REG_CURRENT: {
            cfg_test_get_reg_current_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_test_get_reg_current_t));
            int32_t val = 0;
            sys_pwr_get_bus_current(settings.regulator_num, &val);
            ESP_LOGI(TAG, "sys_pwr_get_bus_current reg=%d -> %ld mA", settings.regulator_num, (long)val);
            break;
        }
        case CFG_TEST_TYPE_GET_SYS_VOLTAGE: {
            uint32_t val = 0;
            sys_pwr_get_bus_voltage(RIK_CHANNEL_TOTAL, &val);
            ESP_LOGI(TAG, "sys_pwr_get_bus_voltage (SYS) -> %lu mV", (unsigned long)val);
            break;
        }
        case CFG_TEST_TYPE_GET_SYS_CURRENT: {
            int32_t val = 0;
            sys_pwr_get_bus_current(RIK_CHANNEL_TOTAL, &val);
            ESP_LOGI(TAG, "sys_pwr_get_bus_current (SYS) -> %ld mA", (long)val);
            break;
        }
        default:
            ESP_LOGW(TAG, "Unknown test packet type: %d", packet_data[0]);
            break;
    }
    return STA_OK;
}