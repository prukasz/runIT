#include "ads7128_mock.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdlib.h>

// --- ADS7128 Register Map Definitions ---
#define ADS7128_REG_SYSTEM_STATUS       0x00
#define ADS7128_REG_GENERAL_CFG         0x01
#define ADS7128_REG_DATA_CFG            0x02
#define ADS7128_REG_OSR_CFG             0x03
#define ADS7128_REG_OPMODE_CFG          0x04
#define ADS7128_REG_PIN_CFG             0x05
#define ADS7128_REG_GPIO_CFG            0x07
#define ADS7128_REG_GPO_DRIVE_CFG       0x09
#define ADS7128_REG_GPO_VALUE           0x0B
#define ADS7128_REG_GPI_VALUE           0x0D
#define ADS7128_REG_SEQUENCE_CFG        0x10
#define ADS7128_REG_CHANNEL_SEL         0x11
#define ADS7128_REG_AUTO_SEQ_CH_SEL     0x12
#define ADS7128_REG_ALERT_CH_SEL        0x14
#define ADS7128_REG_ALERT_MAP           0x16
#define ADS7128_REG_ALERT_PIN_CFG       0x17
#define ADS7128_REG_EVENT_FLAG          0x18
#define ADS7128_REG_EVENT_HIGH_FLAG     0x1A
#define ADS7128_REG_EVENT_LOW_FLAG      0x1C

#define ADS7128_REG_CH0_HIGH_TH         0x21
#define ADS7128_REG_CH0_LOW_TH          0x23
#define ADS7128_REG_CH1_HIGH_TH         0x25
#define ADS7128_REG_CH1_LOW_TH          0x27
#define ADS7128_REG_CH2_HIGH_TH         0x29
#define ADS7128_REG_CH2_LOW_TH          0x2B
#define ADS7128_REG_CH3_HIGH_TH         0x2D
#define ADS7128_REG_CH3_LOW_TH          0x2F
#define ADS7128_REG_CH4_HIGH_TH         0x31
#define ADS7128_REG_CH4_LOW_TH          0x33
#define ADS7128_REG_CH5_HIGH_TH         0x35
#define ADS7128_REG_CH5_LOW_TH          0x37
#define ADS7128_REG_CH6_HIGH_TH         0x39
#define ADS7128_REG_CH6_LOW_TH          0x3B
#define ADS7128_REG_CH7_HIGH_TH         0x3D
#define ADS7128_REG_CH7_LOW_TH          0x3F

// Hysteresis registers
#define ADS7128_REG_CH0_HYST            0x20
#define ADS7128_REG_CH1_HYST            0x24
#define ADS7128_REG_CH2_HYST            0x28
#define ADS7128_REG_CH3_HYST            0x2C
#define ADS7128_REG_CH4_HYST            0x30
#define ADS7128_REG_CH5_HYST            0x34
#define ADS7128_REG_CH6_HYST            0x38
#define ADS7128_REG_CH7_HYST            0x3C

// MAX conversions results
#define ADS7128_REG_MAX_CH0_LSB         0x60
#define ADS7128_REG_MAX_CH0_MSB         0x61
#define ADS7128_REG_MAX_CH1_LSB         0x62
#define ADS7128_REG_MAX_CH1_MSB         0x63
#define ADS7128_REG_MAX_CH2_LSB         0x64
#define ADS7128_REG_MAX_CH2_MSB         0x65
#define ADS7128_REG_MAX_CH3_LSB         0x66
#define ADS7128_REG_MAX_CH3_MSB         0x67
#define ADS7128_REG_MAX_CH4_LSB         0x68
#define ADS7128_REG_MAX_CH4_MSB         0x69
#define ADS7128_REG_MAX_CH5_LSB         0x6A
#define ADS7128_REG_MAX_CH5_MSB         0x6B
#define ADS7128_REG_MAX_CH6_LSB         0x6C
#define ADS7128_REG_MAX_CH6_MSB         0x6D
#define ADS7128_REG_MAX_CH7_LSB         0x6E
#define ADS7128_REG_MAX_CH7_MSB         0x6F

// MIN conversions results
#define ADS7128_REG_MIN_CH0_LSB         0x80
#define ADS7128_REG_MIN_CH0_MSB         0x81
#define ADS7128_REG_MIN_CH1_LSB         0x82
#define ADS7128_REG_MIN_CH1_MSB         0x83
#define ADS7128_REG_MIN_CH2_LSB         0x84
#define ADS7128_REG_MIN_CH2_MSB         0x85
#define ADS7128_REG_MIN_CH3_LSB         0x86
#define ADS7128_REG_MIN_CH3_MSB         0x87
#define ADS7128_REG_MIN_CH4_LSB         0x88
#define ADS7128_REG_MIN_CH4_MSB         0x89
#define ADS7128_REG_MIN_CH5_LSB         0x8A
#define ADS7128_REG_MIN_CH5_MSB         0x8B
#define ADS7128_REG_MIN_CH6_LSB         0x8C
#define ADS7128_REG_MIN_CH6_MSB         0x8D
#define ADS7128_REG_MIN_CH7_LSB         0x8E
#define ADS7128_REG_MIN_CH7_MSB         0x8F

// Recent conversions results
#define ADS7128_REG_RECENT_CH0_LSB      0xA0
#define ADS7128_REG_RECENT_CH0_MSB      0xA1
#define ADS7128_REG_RECENT_CH1_LSB      0xA2
#define ADS7128_REG_RECENT_CH1_MSB      0xA3
#define ADS7128_REG_RECENT_CH2_LSB      0xA4
#define ADS7128_REG_RECENT_CH2_MSB      0xA5
#define ADS7128_REG_RECENT_CH3_LSB      0xA6
#define ADS7128_REG_RECENT_CH3_MSB      0xA7
#define ADS7128_REG_RECENT_CH4_LSB      0xA8
#define ADS7128_REG_RECENT_CH4_MSB      0xA9
#define ADS7128_REG_RECENT_CH5_LSB      0xAA
#define ADS7128_REG_RECENT_CH5_MSB      0xAB
#define ADS7128_REG_RECENT_CH6_LSB      0xAC
#define ADS7128_REG_RECENT_CH6_MSB      0xAD
#define ADS7128_REG_RECENT_CH7_LSB      0xAE
#define ADS7128_REG_RECENT_CH7_MSB      0xAF

// Root mean square (RMS) / Accumulator results - optional limits depending on exact ADS spec
#define ADS7128_REG_ACCUM_CH0_LSB       0xC1
#define ADS7128_REG_ACCUM_CH0_MSB       0xC2
#define ADS7128_REG_ACCUM_CH1_LSB       0xC1
#define ADS7128_REG_ACCUM_CH1_MSB       0xC2
#define ADS7128_REG_ACCUM_CH2_LSB       0xC1

// 117 dostępnych adresów + bufor (np do odczytów auto-inkrementowanych) daje nam max bezpieczny adres np 128 (0x80)
#define ADS_REG_MAX_ADDR 0x100

static const char *TAG = "ADS_MOCK";

static uint8_t ads_registers[ADS_REG_MAX_ADDR];
static bool alert_active = false;
static  void (*alert_callback)(void*) = NULL;
static void* args;

void ads_mock_add_alert_callback(void (*cb)(void*), void* arg) {
    alert_callback = cb;
    args = arg;
}

int ads_mock_get_alert_pin_level(void) {
    return alert_active ? 0 : 1; 
}

void ads_mock_simulate_voltage(uint8_t pin, uint16_t voltage) {
    if (pin >= 8) return;
    
    // Zapisz LSB i MSB dla wybranego kanału do rejestrów RECENT
    ads_registers[ADS7128_REG_RECENT_CH0_LSB + (pin * 2)] = (uint8_t)(voltage & 0xFF);
    ads_registers[ADS7128_REG_RECENT_CH0_MSB + (pin * 2)] = (uint8_t)((voltage >> 8) & 0xFF);
    
    ESP_LOGI(TAG, "[Symulator] Nowe napiecie na wejsciu. Pin %d: %u", pin, voltage);
    
    // Osiem najwyższych bitów z 12-bitowego wyniku dla porównania z progami (zakładamy 12 bitów, więc >> 4)
    uint8_t msb_8bit = (voltage >> 4) & 0xFF;
    
    // Pobranie odpowiednich progów na podstawie aktualnej mapy w mocku
    uint8_t high_th = ads_registers[ADS7128_REG_CH0_HIGH_TH + (pin * 4)];
    uint8_t low_th = ads_registers[ADS7128_REG_CH0_LOW_TH + (pin * 4)];
    
    bool event_high = (msb_8bit > high_th);
    bool event_low = (msb_8bit < low_th);

    // Aktualizacja odpowiednich flag w rejestrach
    if (event_high) {
        ads_registers[ADS7128_REG_EVENT_HIGH_FLAG] |= (1 << pin);
    } else {
        ads_registers[ADS7128_REG_EVENT_HIGH_FLAG] &= ~(1 << pin);
    }
    
    if (event_low) {
        ads_registers[ADS7128_REG_EVENT_LOW_FLAG] |= (1 << pin);
    } else {
        ads_registers[ADS7128_REG_EVENT_LOW_FLAG] &= ~(1 << pin);
    }
    
    if (event_high || event_low) {
        ads_registers[ADS7128_REG_EVENT_FLAG] |= (1 << pin);
    } else {
        ads_registers[ADS7128_REG_EVENT_FLAG] &= ~(1 << pin);
    }

    // Sprawdzenie czy wyzwolone flagi mają uruchomiony alert na danym kanale:
    bool should_alert = false;
    uint8_t active_events = ads_registers[ADS7128_REG_EVENT_FLAG] & ads_registers[ADS7128_REG_ALERT_CH_SEL];
    
    if (active_events > 0) {
        should_alert = true;
    }
    
    if (should_alert != alert_active) {
        alert_active = should_alert;
        ESP_LOGW(TAG, "[Symulator] Stan pinu ALERT zmieniony na: %s", alert_active ? "AKTYWNY (LOW)" : "NIEAKTYWNY (HIGH)");
        if (alert_active && alert_callback) {
            alert_callback(args);
        }
    }
}

esp_err_t ads_mock_transmit(i2c_master_dev_handle_t handle, const uint8_t *write_buffer, size_t write_buffer_len, int xfer_timeout_ms)
{
    if (write_buffer_len == 0) return ESP_ERR_INVALID_ARG;

    // Protokół zapisu ADS7128 obsługuje zarówno:
    // 1. Bez opcode: [Adres Rejestru] [Wartość]
    // 2. Z opcode: [Opcode] [Register Address] [Data...]
    
    if (write_buffer_len >= 2) {
        uint8_t reg_addr = write_buffer[0];
        size_t data_start_idx = 1;  // Indeks rozpoczęcia danych
        
        // Sprawdzenie czy pierwszy bajt to opcode (0x08 = WRITE, 0x28 = CONTINUOUS_WRITE)
        if (write_buffer[0] == 0x08 || write_buffer[0] == 0x28 || write_buffer[0] == 0x18 || write_buffer[0] == 0x20) {
            // Jeśli to opcode, drugi bajt to adres rejestru
            if (write_buffer_len >= 3) {
                reg_addr = write_buffer[1];
                data_start_idx = 2;  // Dane zaczynają się od indeksu 2
            } else {
                return ESP_ERR_INVALID_ARG;  // Za mało bajtów dla formatu z opcode'em
            }
        }
        
        // Zapis danych na kolejne rejestry
        if (reg_addr < ADS_REG_MAX_ADDR) {
            for (size_t i = data_start_idx; i < write_buffer_len && (reg_addr + i - data_start_idx) < ADS_REG_MAX_ADDR; i++) {
                ads_registers[reg_addr + i - data_start_idx] = write_buffer[i];
            }
        }
    }

    return ESP_OK;
}

esp_err_t ads_mock_transmit_receive(i2c_master_dev_handle_t handle, const uint8_t *write_buffer, size_t write_buffer_len, uint8_t *read_buffer, size_t read_buffer_len, int xfer_timeout_ms)
{
    if (write_buffer_len == 0 || read_buffer_len == 0) return ESP_ERR_INVALID_ARG;
    // Założenie - write_buffer zawiera adres rejestru do odczytu
    uint8_t reg_addr = write_buffer[0];
    // Zabezpieczenie dla 2-bajtowych komend (Opcode read: 0x10 SINGLE lub 0x30 CONTINUOUS)
    if (write_buffer_len > 1 && (write_buffer[0] == 0x10 || write_buffer[0] == 0x30)) {
        reg_addr = write_buffer[1];
    }

    //!!!!!!!
    //czemu i dlaczego takie wartości z uint 8 porwnanie 
    if (reg_addr < ADS_REG_MAX_ADDR) {
        for (size_t i = 0; i < read_buffer_len && (reg_addr + i) < ADS_REG_MAX_ADDR; i++) {
            read_buffer[i] = ads_registers[reg_addr + i];
        }
    }

    return ESP_OK;
}
