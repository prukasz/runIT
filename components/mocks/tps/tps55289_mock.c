#include "tps55289_mock.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TPS_MOCK";

#define TPS55289_REG_VOUT_FSB    0x00 // Przypuszczalny rejestr VOUT LSB
#define TPS55289_REG_VOUT_MSB    0x01 // Przypuszczalny rejestr VOUT MSB
#define TPS55289_REG_IOUT_LIMIT  0x02 // Rejestr limitu prądu
#define TPS55289_REG_VOUT_SR     0x03 // Slew rate
#define TPS55289_REG_VOUT_FS     0x04 // Voltage setting
#define TPS55289_REG_INT_MASK    0x05 // Maska przerwań 
#define TPS55289_REG_MODE        0x06 // Rejestr MODE
#define TPS55289_REG_STATUS      0x07 // Rejestr STATUS

typedef struct {
    uint8_t registers[0x10];
    bool int_active; // 0 = fault, 1 = normal
} tps55289_state_t;

static tps55289_state_t tps_state = {
    .registers = {
        [TPS55289_REG_MODE] = 0x20,   // Reset = 00100000b (HICCUP = 1)
        [TPS55289_REG_STATUS] = 0x03, // Reset = 00000011b (STATUS = 11b)
        [TPS55289_REG_INT_MASK] = 0x00,
        [TPS55289_REG_IOUT_LIMIT] = 0x32 // Przykladowa domyslna wartosc
    },
    .int_active = false
};

static tps_int_cb_t tps_int_callback = NULL;

void set_tps_int_callback(tps_int_cb_t cb) {
    tps_int_callback = cb;
}

static void update_tps_int_status(void) {
    uint8_t status = tps_state.registers[TPS55289_REG_STATUS];
    uint8_t mask = tps_state.registers[TPS55289_REG_INT_MASK];
    
    // Sprawdzanie SCP (bit 7), OCP (bit 6), OVP (bit 5) z odpowiednimi maskami
    bool fault_condition = false;
    if ((status & 0x80) && (mask & 0x80)) fault_condition = true; // SCP
    if ((status & 0x40) && (mask & 0x40)) fault_condition = true; // OCP
    if ((status & 0x20) && (mask & 0x20)) fault_condition = true; // OVP

    if (fault_condition != tps_state.int_active) {
        tps_state.int_active = fault_condition;
        ESP_LOGW(TAG, "TPS55289 INT Pin (FB/INT): %s", tps_state.int_active ? "LOW (FAULT)" : "HIGH (NORMAL)");
    }
}

int tps_get_int_pin_level(void) {
    return tps_state.int_active ? 0 : 1; 
}

esp_err_t tps_transmit(i2c_master_dev_handle_t handle, const uint8_t *write_buffer, size_t write_buffer_len, int xfer_timeout_ms) {
    (void)handle;
    (void)xfer_timeout_ms;
    if (write_buffer_len < 2) return ESP_ERR_INVALID_ARG;

    uint8_t reg_addr = write_buffer[0];
    
    ESP_LOGI(TAG, "TRANSMIT Baza: 0x%02X, Len: %d", reg_addr, (int)write_buffer_len - 1);

    for (size_t i = 1; i < write_buffer_len; i++) {
        if(reg_addr < sizeof(tps_state.registers)) {
            // Rejestr STATUS (0x07) jest w wiekszosci Read-Only, wiec ignorujemy zapis do niego poza wybranymi bitami
            if (reg_addr != TPS55289_REG_STATUS) {
                tps_state.registers[reg_addr] = write_buffer[i];
                ESP_LOGI(TAG, "  -> REG 0x%02X = 0x%02X", reg_addr, write_buffer[i]);
            } else {
                ESP_LOGW(TAG, "  -> Praba zapisu do Read-Only REG 0x%02X", reg_addr);
            }
        }
        reg_addr++;
    }
    
    // Zaktualizuj stan pina po ewentualnej zmianie maski
    update_tps_int_status();
    return ESP_OK;
}

esp_err_t tps_transmit_receive(i2c_master_dev_handle_t handle, const uint8_t *write_buffer, size_t write_buffer_len, uint8_t *read_buffer, size_t read_buffer_len, int xfer_timeout_ms) {
    (void)handle;
    (void)xfer_timeout_ms;
    if (write_buffer_len < 1 || read_buffer_len < 1) return ESP_ERR_INVALID_ARG;

    uint8_t reg_addr = write_buffer[0];

    for (size_t i = 0; i < read_buffer_len; i++) {
        if (reg_addr < sizeof(tps_state.registers)) {
            read_buffer[i] = tps_state.registers[reg_addr];
            ESP_LOGI(TAG, "  <- REG 0x%02X = 0x%02X", reg_addr, read_buffer[i]);
            
            // Czytanie STATUS register (0x07) pociaga za soba wyczyszczenie flag bledow (OVP, OCP, SCP)
            if (reg_addr == TPS55289_REG_STATUS) {
                tps_state.registers[TPS55289_REG_STATUS] &= 0x1F; // Kasuje bity 7, 6, 5
                update_tps_int_status();
            }
        } else {
            read_buffer[i] = 0x00;
        }
        reg_addr++;
    }
    return ESP_OK;
}

static void tps55289_simulator_task(void *pvParameters) {
    ESP_LOGI(TAG, "Task TPS55289 Simulator started.");
    uint32_t counter = 0;
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Symulacja zdarzenia co 5 sekund
        counter++;
        
        // Mode symulacji usterki raz na jakiś czas
        if (counter % 3 == 0) {
            uint8_t mode = tps_state.registers[TPS55289_REG_MODE];
            if (mode & 0x80) { // Tylko jeśli OE (Output Enable) ustawione na 1
                ESP_LOGW(TAG, "[Symulator] Wykryto podbicie napiecia! OVP wlaczone.");
                tps_state.registers[TPS55289_REG_STATUS] |= 0x20; // Set OVP (Bit 5)
            }
        }
        
        // Zmieńmy tryb operacji na losowy (Buck/Boost/Buck-Boost)
        tps_state.registers[TPS55289_REG_STATUS] = (tps_state.registers[TPS55289_REG_STATUS] & 0xFC) | (counter % 3);
        
        update_tps_int_status();
    }
}

void init_tps_mock(void) {
    xTaskCreate(tps55289_simulator_task, "tps_sim_task", 2048, NULL, 5, NULL);
}