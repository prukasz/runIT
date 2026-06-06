#include "provider_pwm_expander.h"
#include "manager_io.h"
#include "pca9685.h"
#include "esp_log.h"

#define TAG __FILE_NAME__

#undef OWNER
#define OWNER OWNER_PROVIDER_PWM_EXPANDER
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

status_rep_t p_pca9685_configure(void) {
    CHECK_HANDLE_R(_pca_handle);
    esp_err_t err = pca9685_enable_auto_increment(_pca_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable auto-increment on PCA9685 during configuration");
        return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_PWM_EXPANDER, err);
    }
    return STA_OK;
}

/*******************************************************************************
 * PWM SPECIFIC FUNCTIONS
 ******************************************************************************/

status_rep_t p_pca9685_pwm_set_duty(uint64_t pin_mask, uint32_t duty_cycle) {
    CHECK_HANDLE_R(_pca_handle);
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
    CHECK_HANDLE_R(_pca_handle);
    // Częstotliwość w PCA9685 jest globalna, więc ignorujemy pin_mask, ale zachowujemy spójność API
    (void)pin_mask; 
    esp_err_t err = pca9685_set_pwm_frequency(_pca_handle, (uint16_t)freq_hz);
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
    CHECK_HANDLE_R(_pca_handle);

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


status_rep_t p_pca9685_reset(void) {
    CHECK_HANDLE_R(_pca_handle);

    /* Reset all PWM channels to 0 (off) */
    for (uint8_t i = 0; i < PCA9685_CHANNEL_ALL; i++) {
        esp_err_t err = pca9685_set_pwm_value(_pca_handle, i, 0, true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to reset PWM channel %d during reset_all", i);
            return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_PWM_EXPANDER, err);
        }
        err = pca9685_enable_auto_increment(_pca_handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to enable auto-increment during reset_all for channel %d", i);
            return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_PWM_EXPANDER, err);
        }
    }
    
    ESP_LOGI(TAG, "PWM expander provider reset: all channels set to 0");
    return STA_OK;
}