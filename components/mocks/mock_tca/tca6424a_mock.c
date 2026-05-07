#include "tca6424a_mock.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TCA_MOCK";

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

typedef struct {
    uint8_t input[3];
    uint8_t last_read_input[3];
    uint8_t output[3];
    uint8_t polarity[3];
    uint8_t config[3];
    bool int_active;
} tca6424a_state_t;

static tca6424a_state_t tca_state = {
    .input = {0x00, 0x00, 0x00},
    .last_read_input = {0x00, 0x00, 0x00},
    .output = {0x00, 0x00, 0x00},
    .polarity = {0x00, 0x00, 0x00},
    .config = {0xFF, 0xFF, 0xFF},
    .int_active = false
};

static tca_int_cb_t tca_int_callback = NULL;
static void* tca_int_callback_args = NULL;

void tca_mock_set_intr_callback(tca_int_cb_t cb, void* args) {
    tca_int_callback = cb;
    tca_int_callback_args = args;
}

static void update_int_status(void) {
    bool should_be_active = false;
    for (int i = 0; i < 3; i++) {
        if ((tca_state.input[i] & tca_state.config[i]) != (tca_state.last_read_input[i] & tca_state.config[i])) {
            should_be_active = true;
            break;
        }
    }
    
    if (should_be_active != tca_state.int_active) {
        tca_state.int_active = should_be_active;
        ESP_LOGW(TAG, "Stan pinu INT zmieniony na: %s", tca_state.int_active ? "AKTYWNY (LOW)" : "NIEAKTYWNY (HIGH)");
        if (should_be_active && tca_int_callback) {
            tca_int_callback(tca_int_callback_args);
        }
    }
}

int tca_get_int_pin_level(void) {
    return tca_state.int_active ? 0 : 1; 
}

esp_err_t tca_transmit(i2c_master_dev_handle_t handle, const uint8_t *write_buffer, size_t write_buffer_len, int xfer_timeout_ms) {
    (void)handle;
    (void)xfer_timeout_ms;
    if (write_buffer_len < 2) return ESP_ERR_INVALID_ARG;

    uint8_t reg_addr = write_buffer[0] & 0x7F;
    bool auto_inc = (write_buffer[0] & TCA6424A_AUTO_INCREMENT) != 0;
    
    ESP_LOGI(TAG, "TRANSMIT Baza: 0x%02X, AutoInc: %d, Len: %d", reg_addr, (int)auto_inc, (int)write_buffer_len - 1);

    for (size_t i = 1; i < write_buffer_len; i++) {
        uint8_t val = write_buffer[i];
        uint8_t port = reg_addr % 4;

        if (reg_addr >= TCA6424A_REG_OUTPUT_PORT0 && reg_addr <= TCA6424A_REG_OUTPUT_PORT2) {
            tca_state.output[port] = val;
            ESP_LOGI(TAG, "  -> OUT Port %d = 0x%02X", port, val);
        } else if (reg_addr >= TCA6424A_REG_POLARITY_PORT0 && reg_addr <= TCA6424A_REG_POLARITY_PORT2) {
            tca_state.polarity[port] = val;
            ESP_LOGI(TAG, "  -> POL Port %d = 0x%02X", port, val);
        } else if (reg_addr >= TCA6424A_REG_CONFIG_PORT0 && reg_addr <= TCA6424A_REG_CONFIG_PORT2) {
            tca_state.config[port] = val;
            ESP_LOGI(TAG, "  -> CFG Port %d = 0x%02X", port, val);
            update_int_status();
        }
        if (auto_inc) reg_addr++;
    }
    return ESP_OK;
}

esp_err_t tca_transmit_receive(i2c_master_dev_handle_t handle, const uint8_t *write_buffer, size_t write_buffer_len, uint8_t *read_buffer, size_t read_buffer_len, int xfer_timeout_ms) {
    (void)handle;
    (void)xfer_timeout_ms;
    if (write_buffer_len < 1 || read_buffer_len < 1) return ESP_ERR_INVALID_ARG;

    uint8_t reg_addr = write_buffer[0] & 0x7F;
    bool auto_inc = (write_buffer[0] & TCA6424A_AUTO_INCREMENT) != 0;

    for (size_t i = 0; i < read_buffer_len; i++) {
        uint8_t val = 0;
        uint8_t port = reg_addr % 4;

        if (reg_addr >= TCA6424A_REG_INPUT_PORT0 && reg_addr <= TCA6424A_REG_INPUT_PORT2) {
            tca_state.last_read_input[port] = tca_state.input[port];
            val = tca_state.input[port] ^ tca_state.polarity[port];
            ESP_LOGI(TAG, "  <- IN Port %d = 0x%02X", port, val);
            update_int_status();
        } else if (reg_addr >= TCA6424A_REG_OUTPUT_PORT0 && reg_addr <= TCA6424A_REG_OUTPUT_PORT2) {
            val = tca_state.output[port];
        } else if (reg_addr >= TCA6424A_REG_POLARITY_PORT0 && reg_addr <= TCA6424A_REG_POLARITY_PORT2) {
            val = tca_state.polarity[port];
        } else if (reg_addr >= TCA6424A_REG_CONFIG_PORT0 && reg_addr <= TCA6424A_REG_CONFIG_PORT2) {
            val = tca_state.config[port];
        }

        read_buffer[i] = val;
        if (auto_inc) reg_addr++;
    }
    return ESP_OK;
}

void tca_mock_set_pin_level(uint32_t pin_mask, bool level) {
    uint8_t pin = __builtin_ctz(pin_mask); // Convert pin mask to pin number (0-23)
    if (pin >= 24) return;
    uint8_t port = pin / 8;
    uint8_t bit = pin % 8;
    
    uint8_t old_val = tca_state.input[port];
    if (level) {
        tca_state.input[port] |= (1 << bit);
    } else {
        tca_state.input[port] &= ~(1 << bit);
    }
    if (old_val != tca_state.input[port]) {
        ESP_LOGD(TAG, "[Symulator] Nowy stan fizyczny wejscia. Pin %d z portu %d: %d", pin, port, level);
        update_int_status();
    }
}