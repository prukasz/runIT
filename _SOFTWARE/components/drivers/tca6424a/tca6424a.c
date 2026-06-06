#include "tca6424a.h"

#include "tca6424a_mock.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sdkconfig.h"
#include <esp_log.h>

#define _PORT0_MASK 0x000000FF
#define _PORT1_MASK 0x0000FF00
#define _PORT2_MASK 0x00FF0000

#define TCA6424A_REG_INPUT_PORT0       0x00
#define TCA6424A_REG_INPUT_PORT1       0x01
#define TCA6424A_REG_INPUT_PORT2       0x02
#define TCA6424A_REG_OUTPUT_PORT0      0x04
#define TCA6424A_REG_OUTPUT_PORT1      0x05
#define TCA6424A_REG_OUTPUT_PORT2      0x06
#define TCA6424A_REG_POLARITY_PORT0    0x08
#define TCA6424A_REG_POLARITY_PORT1    0x09
#define TCA6424A_REG_POLARITY_PORT2    0x0A
#define TCA6424A_REG_CONFIG_PORT0      0x0C
#define TCA6424A_REG_CONFIG_PORT1      0x0D
#define TCA6424A_REG_CONFIG_PORT2      0x0E

#define TCA6424A_AUTO_INCREMENT        0x80
#define TCA_ISR_NOTIFICATION_VAL       0x00

#define TAG __FILE_NAME__


#define RETURN_ON_ERROR(x) do {        \
    esp_err_t __err_rc = (x);          \
    if (__err_rc != ESP_OK) return __err_rc; \
} while (0)

#define CHECK_HANDLE_R(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)

static esp_err_t _tca_update_port(tca6424a_handle_t handle, uint8_t port){
    return (esp_err_t)tca_transmit(handle->i2c_dev_handle, (uint8_t[]){TCA6424A_REG_OUTPUT_PORT0 + port, handle->output[port]}, 2, 10);
}

static esp_err_t _tca_update_ports(tca6424a_handle_t handle){
    CHECK_HANDLE_R(handle);
    #if CONFIG_USE_MOCK_TCA6424A
    return tca_transmit(handle->i2c_dev_handle, (uint8_t[]){TCA6424A_REG_OUTPUT_PORT0 | TCA6424A_AUTO_INCREMENT, handle->output[0], handle->output[1], handle->output[2]}, 4, 10);
    #else
    return i2c_master_transmit(handle->i2c_dev_handle, (uint8_t[]){TCA6424A_REG_OUTPUT_PORT0 | TCA6424A_AUTO_INCREMENT, handle->output[0], handle->output[1], handle->output[2]}, 4, 10);
    #endif
}


static esp_err_t _tca_update_inputs(tca6424a_handle_t handle){
    CHECK_HANDLE_R(handle);
    #if GONFIG_USE_MOCK_TCA6424A
    return tca_transmit_receive(handle->i2c_dev_handle, (uint8_t[]){TCA6424A_REG_INPUT_PORT0 | TCA6424A_AUTO_INCREMENT}, 1, handle->last_read_input, 3, 10);
    #else
    return i2c_master_transmit_receive(handle->i2c_dev_handle, (uint8_t[]){TCA6424A_REG_INPUT_PORT0 | TCA6424A_AUTO_INCREMENT}, 1, handle->last_read_input, 3, 10);
    #endif
}

static esp_err_t _tca_update_config(tca6424a_handle_t handle){
    CHECK_HANDLE_R(handle);
    return tca_transmit(handle->i2c_dev_handle, (uint8_t[]){TCA6424A_REG_CONFIG_PORT0 | TCA6424A_AUTO_INCREMENT, handle->config[0], handle->config[1], handle->config[2]}, 4, 10);
    #if CONFIG_USE_MOCK_TCA6424A
    return tca_transmit(handle->i2c_dev_handle, (uint8_t[]){TCA6424A_REG_CONFIG_PORT0 | TCA6424A_AUTO_INCREMENT, handle->config[0], handle->config[1], handle->config[2]}, 4, 10);
    #else
    return i2c_master_transmit(handle->i2c_dev_handle, (uint8_t[]){TCA6424A_REG_CONFIG_PORT0 | TCA6424A_AUTO_INCREMENT, handle->config[0], handle->config[1], handle->config[2]}, 4, 10);
    #endif
}

static esp_err_t _tca_update_polarity(tca6424a_handle_t handle){
    CHECK_HANDLE_R(handle);
    #if CONFIG_USE_MOCK_TCA6424A
    return tca_transmit(handle->i2c_dev_handle, (uint8_t[]){TCA6424A_REG_POLARITY_PORT0 | TCA6424A_AUTO_INCREMENT, handle->polarity_cfg[0], handle->polarity_cfg[1], handle->polarity_cfg[2]}, 4, 10);
    #else
    return i2c_master_transmit(handle->i2c_dev_handle, (uint8_t[]){TCA6424A_REG_POLARITY_PORT0 | TCA6424A_AUTO_INCREMENT, handle->polarity_cfg[0], handle->polarity_cfg[1], handle->polarity_cfg[2]}, 4, 10);
    #endif
}




esp_err_t tca_set_pins(tca6424a_handle_t handle, uint32_t pins_mask, uint32_t pins_state, bool update_now) {
    uint8_t p0_mask = (pins_mask & _PORT0_MASK);
    uint8_t p0_state = (pins_state & _PORT0_MASK);
    handle->output[0] = (handle->output[0] & ~p0_mask) | (p0_state & p0_mask);


    uint8_t p1_mask = (pins_mask & _PORT1_MASK) >> 8; 
    uint8_t p1_state = (pins_state & _PORT1_MASK) >> 8;
    handle->output[1] = (handle->output[1] & ~p1_mask) | (p1_state & p1_mask);


    uint8_t p2_mask = (pins_mask & _PORT2_MASK) >> 16;
    uint8_t p2_state = (pins_state & _PORT2_MASK) >> 16;
    handle->output[2] = (handle->output[2] & ~p2_mask) | (p2_state & p2_mask);
    handle->to_update.ports_to_update = 1;

    if (update_now) {
        handle->to_update.ports_to_update = 0;
        return _tca_update_ports(handle);
    }
    return ESP_OK;
}

esp_err_t tca_get_pins(tca6424a_handle_t handle, uint32_t *out_level, bool force_update) {
    if (force_update) {
        esp_err_t err = _tca_update_inputs(handle);
        if (err != ESP_OK) {
            return err;
        }
    }
    *out_level = (handle->last_read_input[2] << 16) | 
                  (handle->last_read_input[1] << 8) | 
            handle->last_read_input[0];
    return ESP_OK;
}


esp_err_t tca_preset_cfg(tca6424a_handle_t handle, uint32_t cfg_mask, uint32_t cfg_state) {
    uint8_t cfg0_mask = (cfg_mask & _PORT0_MASK);
    uint8_t cfg0_state = (cfg_state & _PORT0_MASK);
    handle->config[0] = (handle->config[0] & ~cfg0_mask) | (cfg0_state & cfg0_mask);

    uint8_t cfg1_mask = (cfg_mask & _PORT1_MASK) >> 8; 
    uint8_t cfg1_state = (cfg_state & _PORT1_MASK) >> 8;
    handle->config[1] = (handle->config[1] & ~cfg1_mask) | (cfg1_state & cfg1_mask);

    uint8_t cfg2_mask = (cfg_mask & _PORT2_MASK) >> 16;
    uint8_t cfg2_state = (cfg_state & _PORT2_MASK) >> 16;
    handle->config[2] = (handle->config[2] & ~cfg2_mask) | (cfg2_state & cfg2_mask);

    return _tca_update_config(handle);
}


esp_err_t tca_set_polarity(tca6424a_handle_t handle, uint32_t polarity_mask, uint32_t polarity_state) {
    uint8_t pol0_mask = (polarity_mask & _PORT0_MASK);
    uint8_t pol0_state = (polarity_state & _PORT0_MASK);
    handle->polarity_cfg[0] = (handle->polarity_cfg[0] & ~pol0_mask) | (pol0_state & pol0_mask);

    uint8_t pol1_mask = (polarity_mask & _PORT1_MASK) >> 8; 
    uint8_t pol1_state = (polarity_state & _PORT1_MASK) >> 8;
    handle->polarity_cfg[1] = (handle->polarity_cfg[1] & ~pol1_mask) | (pol1_state & pol1_mask);

    uint8_t pol2_mask = (polarity_mask & _PORT2_MASK) >> 16;
    uint8_t pol2_state = (polarity_state & _PORT2_MASK) >> 16;
    handle->polarity_cfg[2] = (handle->polarity_cfg[2] & ~pol2_mask) | (pol2_state & pol2_mask);

    return _tca_update_polarity(handle);
}


uint32_t tca_get_pin_output(tca6424a_handle_t handle) {
    return (handle->output[2] << 16) | 
           (handle->output[1] << 8) | 
            handle->output[0];
}

// ISR Callback sets the volatile flag and wakes the task without changing the notification value
void p_gpio_expander_intr_pin_callback(void* arg) {
    tca6424a_handle_t handle = (tca6424a_handle_t)arg;
    BaseType_t high_task_wakeup = pdFALSE;
    
    // Flag that a hardware interrupt occurred
    handle->interrupt_present = true;

    xTaskNotifyFromISR(handle->task_handle, 0, eNoAction, &high_task_wakeup);
    
    if (high_task_wakeup) {
        portYIELD_FROM_ISR();
    }
}

void tca_task(void* dev_handle){
    tca6424a_handle_t handle = (tca6424a_handle_t)dev_handle;

    handle->interrupt_present = false;

    uint32_t previous_state = (handle->last_read_input[2] << 16)| 
                              (handle->last_read_input[1] << 8) | 
                               handle->last_read_input[0];

    while(1){
        uint32_t notification_value = 0;
        xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, portMAX_DELAY);


        if (handle->interrupt_present) {
            handle->interrupt_present = false; // Acknowledge the flag immediately

            if (_tca_update_inputs(handle) == ESP_OK) {
                uint32_t current_state = (handle->last_read_input[2] << 16) | 
                                         (handle->last_read_input[1] << 8) | 
                                          handle->last_read_input[0];

                // Find bits that changed state
                uint32_t changed_bits = current_state ^ previous_state;

                uint32_t rising_edges = changed_bits & current_state;
                uint32_t falling_edges = changed_bits & ~current_state;

                if (changed_bits) {
                    for (int i = 0; i < 24; i++) {
                        bool trigger = false;
                        if (handle->pin_trigger_modes[i] == TCA_ON_RISING_EDGE && (rising_edges & (1 << i))) {
                            trigger = true;
                        } else if (handle->pin_trigger_modes[i] == TCA_ON_FALLING_EDGE && (falling_edges & (1 << i))) {
                            trigger = true;
                        } else if (handle->pin_trigger_modes[i] == TCA_ON_CHANGE && (changed_bits & (1 << i))) {
                            trigger = true;
                        }

                        if (trigger && handle->callbacks[i]) {
                            handle->callbacks[i](handle->callback_args[i]);
                        }
                    }
                }
                // Save state for the next comparison
                previous_state = current_state;
            }
        }


        if (notification_value != 0) {
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
            TaskHandle_t caller_task = (TaskHandle_t)(uintptr_t)notification_value;
            #pragma GCC diagnostic pop
            esp_err_t err;
            if (handle->to_update.ports_to_update) {
                err =_tca_update_ports(handle);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to update ports in task");
                    xTaskNotify(caller_task, 1, eSetBits);
                    goto TASK_END;
                }
                handle->to_update.ports_to_update = 0;
            }

            xTaskNotify(caller_task, 0, eSetBits);
        }
        TASK_END:
        continue;
    }
}


esp_err_t tca_register_pin_callback(tca6424a_handle_t handle, uint32_t pin_mask, void (*cb)(void*), tca_interrupt_mode_e mode, void* arg) {
    uint8_t pin = __builtin_ctz(pin_mask); 
    if (pin < 24) {
        handle->pin_trigger_modes[pin] = mode;
        handle->callbacks[pin] = cb;
        handle->callback_args[pin] = arg;
        return ESP_OK;
        ESP_LOGI(TAG, "Registered callback for pin %d with mode %d", pin, mode);
    }

    ESP_LOGW(TAG, "Attempted to register callback for invalid pin %d, available pins are 0-23", pin);    
    return ESP_ERR_INVALID_ARG;
}


tca6424a_handle_t tca_new(uint8_t i2c_address) {
    tca6424a_handle_t handle = calloc(1, sizeof(tca_data_t));
    xTaskCreate(tca_task, NULL, 4096, handle, 10, &handle->task_handle);
    handle->i2c_dev_config.device_address = i2c_address;
    handle->i2c_dev_config.scl_speed_hz = 100000;
    handle->i2c_dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    return handle;
}

