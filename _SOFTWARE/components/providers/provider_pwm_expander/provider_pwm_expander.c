#include "provider_pwm_expander.h"
#include "manager_io.h"
#include "pca9685.h"
#include "esp_log.h"

#define TAG __FILE_NAME__

static pca9685_handle_t _pca_handle = NULL;
static bool _freeze = false;

void* p_pca9685_new(uint8_t i2c_addr) {
    _pca_handle = pca9685_new(i2c_addr);
    return (void*)_pca_handle;
}

i2c_device_config_t* p_pca9685_get_i2c_dev_config(void) {
    return &(_pca_handle->i2c_device_config);
}

i2c_master_dev_handle_t* p_pca9685_get_i2c_dev_handle(void) {
    return &(_pca_handle->i2c_dev_handle);
}

TaskHandle_t p_pca9685_get_task_handle(void) {
    return _pca_handle->driver_task_handle;
}

void p_pca9685_freeze(bool freeze) {
    _freeze = freeze;
}

/*******************************************************************************
 * PWM SPECIFIC FUNCTIONS
 ******************************************************************************/

status_rep_t p_pca9685_pwm_set_duty(uint64_t pin_mask, uint32_t duty_cycle) {
    if (!_pca_handle) {
        return STA_C(IO_ERR_PORT_INVALID, OWNER_PROVIDER_PWM_EXPANDER, 0);
    }
    
    // Zabezpieczenie przed przekroczeniem 12-bitowej rozdzielczości PCA9685
    if (duty_cycle > PCA9685_MAX_PWM_VALUE) {
        duty_cycle = PCA9685_MAX_PWM_VALUE;
    }
    // Aplikowanie wartości PWM dla każdego bitu w masce
    for(uint8_t i = 0; i < PCA9685_CHANNEL_ALL; i++) {
        if (pin_mask & (1ULL << i)) {
            esp_err_t err = pca9685_set_pwm_value(_pca_handle, i, (uint16_t)duty_cycle, !_freeze);
            if (err != ESP_OK) {
                // Przekazujemy kod błędu ESP do kontekstu naszego rich-error object
                return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_PWM_EXPANDER, err);
            }
        }
    }
    return STA_OK;
}

status_rep_t p_pca9685_pwm_set_freq(uint64_t pin_mask, uint32_t freq_hz) {
    if (!_pca_handle) {
        return STA_C(IO_ERR_PORT_INVALID, OWNER_PROVIDER_PWM_EXPANDER, 0);
    }
    
    // Częstotliwość w PCA9685 jest globalna, więc ignorujemy pin_mask, ale zachowujemy spójność API
    (void)pin_mask; 
    esp_err_t err = pca9685_set_pwm_frequency(_pca_handle, (uint16_t)freq_hz, !_freeze);
    if (err != ESP_OK) {
        return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_PWM_EXPANDER, err);
    }
    
    return STA_OK;
}

/*******************************************************************************
 * GPIO LIKE FUNCTIONS (Set / Toggle)
 ******************************************************************************/

status_rep_t p_pca9685_set_pins(uint64_t pin_mask, bool state) {
    // Tłumaczymy stan logiczny na pełne wypełnienie (4095) lub całkowite wyłączenie (0)
    uint16_t target_pwm = state ? PCA9685_MAX_PWM_VALUE : 0;
    
    // Wykorzystujemy już zabezpieczoną metodę PWM (zwróci gotowy status_rep_t)
    return p_pca9685_pwm_set_duty(pin_mask, target_pwm);
}

status_rep_t p_pca9685_toggle_pins(uint64_t pin_mask) {
    if (!_pca_handle) {
        return STA_C(IO_ERR_PORT_INVALID, OWNER_PROVIDER_PWM_EXPANDER, 0);
    }

    for (uint8_t i = 0; i < PCA9685_CHANNEL_ALL; i++) {
        if (pin_mask & (1ULL << i)) {
            // Odczyt aktualnego stanu z lokalnego cache struktury PCA9685
            // Wartość >= 50% wypełnienia traktujemy jako HIGH (załączony)
            uint16_t current_val = _pca_handle->channel_pwm_value[i];
            uint16_t new_val = (current_val >= (PCA9685_MAX_PWM_VALUE / 2)) ? 0 : PCA9685_MAX_PWM_VALUE;
            
            esp_err_t err = pca9685_set_pwm_value(_pca_handle, i, new_val, !_freeze);
            if (err != ESP_OK) {
                // Rzucenie błędu w przypadku potknięcia I2C / asynchronicznej kolejki
                return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_PWM_EXPANDER, err);
            }
        }
    }
    return STA_OK;
}

void p_pca9685_notify_to_update(void) {
    if (_pca_handle) {
        // Powiadomienie zadania sterującego o potrzebie aktualizacji (np. po odblokowaniu)
        xTaskNotify(_pca_handle->driver_task_handle, 0, eNoAction);
    }
}