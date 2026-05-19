#include "tps55289_mock.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"

#define TAG __FILE_NAME__

#define TPS55289_REG_REF_LSB     0x00
#define TPS55289_REG_REF_MSB     0x01
#define TPS55289_REG_IOUT_LIMIT  0x02
#define TPS55289_REG_VOUT_SR     0x03
#define TPS55289_REG_VOUT_FS     0x04
#define TPS55289_REG_CDC         0x05
#define TPS55289_REG_MODE        0x06
#define TPS55289_REG_STATUS      0x07

#define TPS_ADDR_1 0x74
#define TPS_ADDR_2 0x75

typedef struct {
    uint8_t i2c_addr;
    uint8_t registers[0x10];
    bool int_active; // true = fault (LOW pin), false = normal (HIGH pin)
    tps_int_cb_t int_callback[2];
    void* int_callback_args[2];
} tps55289_state_t;

#define TPS_DEFAULT_REGS { \
    [TPS55289_REG_REF_LSB]    = 0xA4, \
    [TPS55289_REG_REF_MSB]    = 0x01, \
    [TPS55289_REG_IOUT_LIMIT] = 0xE4, \
    [TPS55289_REG_VOUT_SR]    = 0x01, \
    [TPS55289_REG_VOUT_FS]    = 0x03, \
    [TPS55289_REG_CDC]        = 0xE0, \
    [TPS55289_REG_MODE]       = 0x20, \
    [TPS55289_REG_STATUS]     = 0x03  \
}

// Statyczna inicjalizacja mocków dla adresów 0x74 i 0x75
static tps55289_state_t tps_devices[2] = {
    { .i2c_addr = TPS_ADDR_1, .registers = TPS_DEFAULT_REGS, .int_active = false, .int_callback = {NULL, NULL}, .int_callback_args = {NULL, NULL} },
    { .i2c_addr = TPS_ADDR_2, .registers = TPS_DEFAULT_REGS, .int_active = false, .int_callback = {NULL, NULL}, .int_callback_args = {NULL, NULL} }
};

static tps55289_state_t* get_device_by_addr(uint8_t addr) {
    if (addr == TPS_ADDR_1) return &tps_devices[0];
    if (addr == TPS_ADDR_2) return &tps_devices[1];
    return NULL;
}

void tps_mock_set_intr_callbacks(uint8_t i2c_addr, tps_int_cb_t cb_active, void* args_active, tps_int_cb_t cb_inactive, void* args_inactive) {
    tps55289_state_t* dev = get_device_by_addr(i2c_addr);
    if (dev) {
        dev->int_callback[0] = cb_active;
        dev->int_callback_args[0] = args_active;
        dev->int_callback[1] = cb_inactive;
        dev->int_callback_args[1] = args_inactive;
    }
}

static void update_tps_int_status(tps55289_state_t* dev) {
    uint8_t status = dev->registers[TPS55289_REG_STATUS];
    uint8_t mask = dev->registers[TPS55289_REG_CDC]; 
    
    // Check SCP (bit 7), OCP (bit 6), OVP (bit 5) against masks
    bool fault_condition = false;
    if ((status & 0x80) && (mask & 0x80)) fault_condition = true; // SCP
    if ((status & 0x40) && (mask & 0x40)) fault_condition = true; // OCP
    if ((status & 0x20) && (mask & 0x20)) fault_condition = true; // OVP

    if (fault_condition != dev->int_active) {
        dev->int_active = fault_condition;
        ESP_LOGW(TAG, "TPS55289 [0x%02X] INT Pin: %s", dev->i2c_addr, dev->int_active ? "LOW (FAULT)" : "HIGH (NORMAL)");

        uint8_t cb_idx = dev->int_active ? 0 : 1;
        if (dev->int_callback[cb_idx]) {
            dev->int_callback[cb_idx](dev->int_callback_args[cb_idx]);
        }
    }
}

// --- FUNKCJE WYZWALAJĄCE BŁĘDY ---

void tps_trigger_ovp(uint8_t i2c_addr) {
    tps55289_state_t* dev = get_device_by_addr(i2c_addr);
    if (dev) {
        ESP_LOGE(TAG, "[0x%02X] Triggering OVP (Over-Voltage Protection)", i2c_addr);
        dev->registers[TPS55289_REG_STATUS] |= 0x20; // Set OVP (Bit 5)
        update_tps_int_status(dev);
    }
}

void tps_trigger_ocp(uint8_t i2c_addr) {
    tps55289_state_t* dev = get_device_by_addr(i2c_addr);
    if (dev) {
        ESP_LOGE(TAG, "[0x%02X] Triggering OCP (Over-Current Protection)", i2c_addr);
        dev->registers[TPS55289_REG_STATUS] |= 0x40; // Set OCP (Bit 6)
        update_tps_int_status(dev);
    }
}

void tps_trigger_scp(uint8_t i2c_addr) {
    tps55289_state_t* dev = get_device_by_addr(i2c_addr);
    if (dev) {
        ESP_LOGE(TAG, "[0x%02X] Triggering SCP (Short-Circuit Protection)", i2c_addr);
        dev->registers[TPS55289_REG_STATUS] |= 0x80; // Set SCP (Bit 7)
        update_tps_int_status(dev);
    }
}

// --- ZMODYFIKOWANA KOMUNIKACJA I2C ---

esp_err_t tps_transmit(uint8_t i2c_addr, const uint8_t *write_buffer, size_t write_buffer_len, int xfer_timeout_ms) {
    (void)xfer_timeout_ms;
    if (write_buffer_len < 2) return ESP_ERR_INVALID_ARG;

    tps55289_state_t* dev = get_device_by_addr(i2c_addr);
    if (dev == NULL) {
        ESP_LOGE(TAG, "TRANSMIT: Address 0x%02X not recognized!", i2c_addr);
        return ESP_ERR_NOT_FOUND;
    }
    

    uint8_t reg_addr = write_buffer[0];
    ESP_LOGI(TAG, "TRANSMIT [0x%02X] Base REG: 0x%02X, Len: %d", i2c_addr, reg_addr, (int)write_buffer_len - 1);

    for (size_t i = 1; i < write_buffer_len; i++) {
        if(reg_addr < sizeof(dev->registers)) {
            if (reg_addr == TPS55289_REG_STATUS) {
                dev->registers[reg_addr] = (dev->registers[reg_addr] & 0xE3) | (write_buffer[i] & 0x1C);
            } else {
                dev->registers[reg_addr] = write_buffer[i];
            }
            ESP_LOGI(TAG, "  -> REG 0x%02X = 0x%02X", reg_addr, dev->registers[reg_addr]);
        }
        reg_addr++;
    }
    
    update_tps_int_status(dev);
    return ESP_OK;
}

esp_err_t tps_transmit_receive(uint8_t i2c_addr, const uint8_t *write_buffer, size_t write_buffer_len, uint8_t *read_buffer, size_t read_buffer_len, int xfer_timeout_ms) {
    (void)xfer_timeout_ms;
    if (write_buffer_len < 1 || read_buffer_len < 1) return ESP_ERR_INVALID_ARG;

    tps55289_state_t* dev = get_device_by_addr(i2c_addr);
    if (dev == NULL) {
        ESP_LOGE(TAG, "RECEIVE: Address 0x%02X not recognized!", i2c_addr);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t reg_addr = write_buffer[0];

    for (size_t i = 0; i < read_buffer_len; i++) {
        if (reg_addr < sizeof(dev->registers)) {
            read_buffer[i] = dev->registers[reg_addr];
            ESP_LOGI(TAG, "  <- [0x%02X] REG 0x%02X = 0x%02X", i2c_addr, reg_addr, read_buffer[i]);
            
            // Wyczyszczenie błędów po odczycie rejestru STATUS
            if (reg_addr == TPS55289_REG_STATUS) {
                dev->registers[TPS55289_REG_STATUS] &= 0x1F; 
                update_tps_int_status(dev); 
            }
        } else {
            read_buffer[i] = 0x00;
        }
        reg_addr++;
    }
    return ESP_OK;
}