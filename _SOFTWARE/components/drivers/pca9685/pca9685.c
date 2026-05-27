#include "pca9685.h"
#include <string.h>
#include <math.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include "freertos/task.h"

#define TAG "PCA9685"

#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)
#define RETURN_ON_ERROR(x) do {        \
    esp_err_t __err_rc = (x);          \
    if (__err_rc != ESP_OK) return __err_rc; \
} while (0)

#define REG_LED_N(x)  (REG_LED_START  + (x) * 4)  // Generowanie adresu dla konkretnego kanału

#define PCA9685_INTERNAL_FREQ 25000000UL
#define WAKEUP_DELAY_US 500

#define PCA9685_ALLCALLADR_DEFAULT 0x70
#define PCA9685_SB1_DEFAULT  0xE2
#define PCA9685_SB2_DEFAULT  0xE4
#define PCA9685_SB3_DEFAULT  0xE8

#define REG_MODE1      0x00
#define REG_MODE2      0x01
#define REG_SUBADR1    0x02
#define REG_ALLCALLADR 0x05
#define REG_LED_START  0x06
#define REG_ALL_LED    0xFA
#define REG_PRE_SCALE  0xFE

#define MODE1_RESTART_BIT   (1 << 7)
#define MODE1_SLEEP_BIT     (1 << 4)
#define MODE1_SUB_CNT       3
#define MODE1_AI            (1 << 5)

#define MODE2_INVRT_BIT     (1 << 4)
#define MODE2_OUTDRV_BIT    (1 << 2)

#define LED_FULL_ON_OFF     (1 << 4) // Ustawia wyjście w stan stale włączony/wyłączony

#define MIN_PRESCALER 0x03
#define MAX_PRESCALER 0xFF
#define MAX_SUBADDR   2

// Deklaracja Taska dla RTOS
void pca9685_task(void *arg);

/*******************************************************************************
 * HELPER FUNCTIONS (Wewnętrzne)
 ******************************************************************************/

static inline esp_err_t _update_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t mask, uint8_t val)
{
    uint8_t r;
    // Odczyt aktualnej wartości rejestru
    RETURN_ON_ERROR(i2c_master_transmit_receive(dev_handle, &reg, 1, &r, 1, PCA9685_TIMEOUT_MS));
    // Wyczyszczenie odpowiednich bitów i ustawienie nowych
    r = (r & ~mask) | val;
    // Zapis z powrotem do układu
    RETURN_ON_ERROR(i2c_master_transmit(dev_handle, (uint8_t[]){reg, r}, 2, PCA9685_TIMEOUT_MS));
    return ESP_OK;
}

// Stara blokująca funkcja do preskalera (używana jeśli immediate == true)
static esp_err_t _pca9685_set_prescaler_blocking(pca9685_handle_t handle, uint8_t prescaler_val)
{
    uint8_t mode;
    RETURN_ON_ERROR(i2c_master_transmit_receive(handle->i2c_dev_handle, (uint8_t[]){ REG_MODE1 }, 1, &mode, 1, PCA9685_TIMEOUT_MS));
    RETURN_ON_ERROR(_update_reg(handle->i2c_dev_handle, REG_MODE1, MODE1_SLEEP_BIT, MODE1_SLEEP_BIT));
    RETURN_ON_ERROR(i2c_master_transmit(handle->i2c_dev_handle, (uint8_t[]){ REG_PRE_SCALE, prescaler_val }, 2, PCA9685_TIMEOUT_MS));
    RETURN_ON_ERROR(_update_reg(handle->i2c_dev_handle, REG_MODE1, MODE1_SLEEP_BIT, 0));

    handle->mode1.reg_val = (mode & ~MODE1_SLEEP_BIT);
    return ESP_OK;
}

/*******************************************************************************
 * CORE INITIALIZATION & TASK
 ******************************************************************************/

pca9685_handle_t pca9685_new(uint8_t i2c_address)
{
    // Alokacja i wyzerowanie pamięci (zastępuje malloc + memset)
    pca9685_handle_t handle = calloc(1, sizeof(_pca9685_data_t));
    if (!handle)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for PCA9685 handle");
        return NULL;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_address,
        .scl_speed_hz = PCA9685_I2C_DEFAULT_FREQUENCY
    };

    handle->i2c_device_config = dev_cfg;
    
    // Wartości domyślne dla adresów grupowych
    handle->sub_value[0] = PCA9685_SB1_DEFAULT;
    handle->sub_value[1] = PCA9685_SB2_DEFAULT;
    handle->sub_value[2] = PCA9685_SB3_DEFAULT;
    handle->allcalladr   = PCA9685_ALLCALLADR_DEFAULT;
    
    // Ustawienie flag do odłożonej inicjalizacji (Lazy Init)
    handle->mode1.AI = 1; 
    handle->to_update.update_mode1 = 1; 

    // Utworzenie dedykowanego Taska w tle
    if (xTaskCreate(pca9685_task, "pca9685_task", 4096, handle, 5, &handle->driver_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create PCA9685 task");
        free(handle);
        return NULL;
    }

    ESP_LOGI(TAG, "PCA9685 driver instance created for 0x%02X", i2c_address);
    return handle;
}

void pca9685_task(void *arg)
{
    pca9685_handle_t handle = (pca9685_handle_t)arg;
    uint32_t notification_value;

    while (1)
    {
        notification_value = 0;
        xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, portMAX_DELAY);

        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
        TaskHandle_t caller_task = (TaskHandle_t)(uintptr_t)notification_value;
        #pragma GCC diagnostic pop

        esp_err_t err = ESP_OK;

        // --- 1. Odłożona inicjalizacja sprzętowa (MODE1 / Auto-Increment) ---
        if (handle->to_update.update_mode1) {
            err = i2c_master_transmit(handle->i2c_dev_handle, 
                                      (uint8_t[]){REG_MODE1, handle->mode1.reg_val}, 
                                      2, PCA9685_TIMEOUT_MS);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Hardware auto-init failed (MODE1)");
                goto TASK_RESPOND;
            }
            handle->to_update.update_mode1 = 0;
        }

        // --- 2. Asynchroniczna zmiana logiki wyjść (MODE2) ---
        if (handle->to_update.update_mode2) {
            err = i2c_master_transmit(handle->i2c_dev_handle, 
                                      (uint8_t[]){REG_MODE2, handle->mode2.reg_val}, 
                                      2, PCA9685_TIMEOUT_MS);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to update MODE2 in background");
                goto TASK_RESPOND;
            }
            handle->to_update.update_mode2 = 0;
        }

        // --- 3. Asynchroniczna zmiana częstotliwości (Prescaler) ---
        if (handle->to_update.update_prescaler) {
            uint8_t sleep_mode = (handle->mode1.reg_val & ~MODE1_RESTART_BIT) | MODE1_SLEEP_BIT;
            
            // Krok A: Uśpij
            err = i2c_master_transmit(handle->i2c_dev_handle, (uint8_t[]){REG_MODE1, sleep_mode}, 2, PCA9685_TIMEOUT_MS);
            // Krok B: Zapisz nowy preskaler
            if (err == ESP_OK) {
                err = i2c_master_transmit(handle->i2c_dev_handle, (uint8_t[]){REG_PRE_SCALE, handle->prescale}, 2, PCA9685_TIMEOUT_MS);
            }
            // Krok C: Wybudź
            if (err == ESP_OK) {
                err = i2c_master_transmit(handle->i2c_dev_handle, (uint8_t[]){REG_MODE1, handle->mode1.reg_val}, 2, PCA9685_TIMEOUT_MS);
            }

            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to update Prescaler in background");
                goto TASK_RESPOND;
            }
            handle->to_update.update_prescaler = 0;
        }

        // --- 4. Asynchroniczna zmiana wartości PWM ---
        if (handle->to_update.update_pwm) {
            err = pca9685_update_pwm_values(handle, handle->pwm_update_mask);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to update PWM values in background");
                goto TASK_RESPOND;
            }
            handle->to_update.update_pwm = 0;
            handle->pwm_update_mask = 0;
        }

TASK_RESPOND:
        // Jeśli zadanie wywołujące przekazało swój uchwyt, odpowiadamy statusem
        if (caller_task) {
            xTaskNotify(caller_task, (err == ESP_OK ? 0 : 1), eSetBits);
        }
    }
}

/*******************************************************************************
 * PWM CONTROL FUNCTIONS
 ******************************************************************************/

esp_err_t pca9685_set_pwm_value(pca9685_handle_t handle, uint8_t channel, uint16_t value, bool immediate)
{
    CHECK_ARG(handle && channel <= PCA9685_CHANNEL_ALL && value <= PCA9685_MAX_PWM_VALUE);
    
    // Zapisz wartość w lokalnym cache
    if (channel < PCA9685_CHANNEL_ALL) {
        handle->channel_pwm_value[channel] = value;
    }

    if (immediate) {
        // Transmisja natychmiastowa (blokująca)
        uint8_t reg = (channel == PCA9685_CHANNEL_ALL) ? REG_ALL_LED : REG_LED_N(channel);

        bool full_on  = (value == PCA9685_MAX_PWM_VALUE);
        bool full_off = (value == 0);
        uint16_t raw = full_on ? 0x0FFF : value;

        uint8_t buf[5];
        buf[0] = reg;
        buf[1] = 0;
        buf[2] = full_on ? LED_FULL_ON_OFF : 0;
        buf[3] = raw & 0xFF;
        buf[4] = full_off ? (LED_FULL_ON_OFF | (raw >> 8)) : (raw >> 8);
        return i2c_master_transmit(handle->i2c_dev_handle, buf, sizeof(buf), PCA9685_TIMEOUT_MS);
    } 
    else {
        // Asynchroniczne zlecenie do Taska
        if (channel == PCA9685_CHANNEL_ALL) {
            handle->pwm_update_mask = 0xFFFF; // Wszystkie
        } else {
            handle->pwm_update_mask |= (1 << channel); // Wybrany
        }
        
        handle->to_update.update_pwm = 1;
        return ESP_OK;
    }
}

esp_err_t pca9685_update_pwm_values(pca9685_handle_t handle, uint16_t bitmask)
{
    CHECK_ARG(handle);

    // Pełen update (szybka wysyłka całego bloku rejestrów, wymaga włączonego AI)
    if (bitmask == 0 || bitmask == 0xFFFF)
    {
        uint8_t buf[1 + PCA9685_CHANNEL_ALL * 4];
        buf[0] = REG_LED_N(0); // Zaczynamy od LED0_ON_L

        for (uint8_t ch = 0; ch < PCA9685_CHANNEL_ALL; ch++)
        {
            uint16_t val = handle->channel_pwm_value[ch];
            bool full_on  = (val == PCA9685_MAX_PWM_VALUE);
            bool full_off = (val == 0);
            uint16_t raw  = full_on ? 4095 : val;

            buf[1 + ch * 4 + 0] = 0;
            buf[1 + ch * 4 + 1] = full_on ? LED_FULL_ON_OFF : 0;
            buf[1 + ch * 4 + 2] = raw & 0xFF;
            buf[1 + ch * 4 + 3] = full_off ? (LED_FULL_ON_OFF | (raw >> 8)) : (raw >> 8);
        }

        return i2c_master_transmit(handle->i2c_dev_handle, buf, sizeof(buf), PCA9685_TIMEOUT_MS);
    }

    // Częściowy update wybranych kanałów
    for (uint8_t ch = 0; ch < PCA9685_CHANNEL_ALL; ch++)
    {
        if (!(bitmask & (1 << ch))) { continue; }

        uint16_t val = handle->channel_pwm_value[ch];
        bool full_on  = (val == PCA9685_MAX_PWM_VALUE);
        bool full_off = (val == 0);
        uint16_t raw  = full_on ? 4095 : val;

        uint8_t buf[5];
        buf[0] = REG_LED_N(ch);
        buf[1] = 0;
        buf[2] = full_on ? LED_FULL_ON_OFF : 0;
        buf[3] = raw & 0xFF;
        buf[4] = full_off ? (LED_FULL_ON_OFF | (raw >> 8)) : (raw >> 8);

        RETURN_ON_ERROR(i2c_master_transmit(handle->i2c_dev_handle, buf, sizeof(buf), PCA9685_TIMEOUT_MS));
    }

    return ESP_OK;
}

/*******************************************************************************
 * CONFIGURATION FUNCTIONS (Z opcją immediate)
 ******************************************************************************/

esp_err_t pca9685_set_pwm_frequency(pca9685_handle_t handle, uint16_t freq, bool immediate)
{
    CHECK_ARG(handle && freq != 0);
    
    // Obliczanie wartości preskalera
    uint8_t prescaler = (uint8_t)(round((float)PCA9685_INTERNAL_FREQ / (PCA9685_MAX_PWM_VALUE * freq))) - 1;
    CHECK_ARG(prescaler >= MIN_PRESCALER);

    // Zmiana w lokalnym cache
    handle->freq = freq;
    handle->prescale = prescaler;

    if (immediate) {
        return _pca9685_set_prescaler_blocking(handle, prescaler);
    } else {
        handle->to_update.update_prescaler = 1;
        return ESP_OK;
    }
}

esp_err_t pca9685_set_output_inverted(pca9685_handle_t handle, bool inverted, bool immediate)
{
    CHECK_ARG(handle);
    handle->mode2.INVRT = inverted ? 1 : 0;

    if (immediate) {
        uint8_t val = inverted ? MODE2_INVRT_BIT : 0;
        return _update_reg(handle->i2c_dev_handle, REG_MODE2, MODE2_INVRT_BIT, val);
    } else {
        handle->to_update.update_mode2 = 1;
        return ESP_OK;
    }
}

esp_err_t pca9685_set_output_open_drain(pca9685_handle_t handle, bool od, bool immediate)
{
    CHECK_ARG(handle);
    handle->mode2.OUTDRV = od ? 0 : 1; 

    if (immediate) {
        uint8_t val = od ? 0 : MODE2_OUTDRV_BIT; // open-drain = 0, totem-pole = 1
        return _update_reg(handle->i2c_dev_handle, REG_MODE2, MODE2_OUTDRV_BIT, val);
    } else {
        handle->to_update.update_mode2 = 1;
        return ESP_OK;
    }
}

/*******************************************************************************
 * LEGACY CONFIGURATION (Rzadko wywoływane, pozostawione jako blokujące)
 ******************************************************************************/

esp_err_t pca9685_get_prescaler_and_freq(pca9685_handle_t handle)
{
    CHECK_ARG(handle);
    RETURN_ON_ERROR(i2c_master_transmit_receive(handle->i2c_dev_handle, (uint8_t[]){ REG_PRE_SCALE }, 1, &handle->prescale, 1, PCA9685_TIMEOUT_MS));
    handle->freq = (uint16_t)(PCA9685_INTERNAL_FREQ / ((uint32_t)PCA9685_MAX_PWM_VALUE * (handle->prescale + 1)));
    return ESP_OK;
}

esp_err_t pca9685_set_subaddress(pca9685_handle_t handle, uint8_t num, uint8_t address_val, bool en)
{
    CHECK_ARG(handle);
    if (num > MAX_SUBADDR) { return ESP_ERR_INVALID_ARG; }

    uint8_t reg = REG_SUBADR1 + num;
    uint8_t data = address_val << 1;

    RETURN_ON_ERROR(i2c_master_transmit(handle->i2c_dev_handle, (uint8_t[]){reg, data}, 2, PCA9685_TIMEOUT_MS));
    
    uint8_t mask = 1 << (MODE1_SUB_CNT - num);
    RETURN_ON_ERROR(_update_reg(handle->i2c_dev_handle, REG_MODE1, mask, en ? mask : 0));
    
    handle->mode1.reg_val = (handle->mode1.reg_val & ~(1 << (MODE1_SUB_CNT - num))) | (en << (MODE1_SUB_CNT - num));
    handle->sub_value[num] = address_val;

    return ESP_OK;
}

esp_err_t pca9685_restart(pca9685_handle_t handle)
{
    CHECK_ARG(handle);
    uint8_t mode;
    RETURN_ON_ERROR(i2c_master_transmit_receive(handle->i2c_dev_handle, (uint8_t[]){ REG_MODE1 }, 1, &mode, 1, PCA9685_TIMEOUT_MS));

    if (mode & MODE1_RESTART_BIT)
    {
        uint8_t data[2] = {REG_MODE1, mode & ~MODE1_SLEEP_BIT};
        RETURN_ON_ERROR(i2c_master_transmit(handle->i2c_dev_handle, data, 2, PCA9685_TIMEOUT_MS));
        esp_rom_delay_us(WAKEUP_DELAY_US);
    }

    uint8_t new_mode[2] = {REG_MODE1, (mode & ~MODE1_SLEEP_BIT) | MODE1_RESTART_BIT};
    RETURN_ON_ERROR(i2c_master_transmit(handle->i2c_dev_handle, new_mode, 2, PCA9685_TIMEOUT_MS));

    handle->mode1.SLEEP = false;
    return ESP_OK;
}

esp_err_t pca9685_sleep(pca9685_handle_t handle, bool sleep)
{
    CHECK_ARG(handle);
    uint8_t val = sleep ? MODE1_SLEEP_BIT : 0;
    ESP_ERROR_CHECK(_update_reg(handle->i2c_dev_handle, REG_MODE1, (uint8_t)MODE1_SLEEP_BIT, val));
    if (!sleep) { esp_rom_delay_us(WAKEUP_DELAY_US); }
    return ESP_OK;
}

esp_err_t pca9685_read_modes_reg(pca9685_handle_t handle)
{
    CHECK_ARG(handle);
    RETURN_ON_ERROR(i2c_master_transmit_receive(handle->i2c_dev_handle,
         (uint8_t[]){ REG_MODE1 }, 1, &handle->mode1.reg_val, 1, PCA9685_TIMEOUT_MS));
    RETURN_ON_ERROR(i2c_master_transmit_receive(handle->i2c_dev_handle,
         (uint8_t[]){ REG_MODE2 }, 1, &handle->mode2.reg_val, 1, PCA9685_TIMEOUT_MS));

    return ESP_OK;
}

esp_err_t pca9685_write_modes_reg(pca9685_handle_t handle, uint8_t reg)
{
    CHECK_ARG(handle);
    if (reg == 1) {
        return i2c_master_transmit(handle->i2c_dev_handle, (uint8_t[]){REG_MODE1, handle->mode1.reg_val}, 2, PCA9685_TIMEOUT_MS); 
    }
    else if (reg == 2) {
        return i2c_master_transmit(handle->i2c_dev_handle, (uint8_t[]){REG_MODE2, handle->mode2.reg_val}, 2, PCA9685_TIMEOUT_MS);    
    }
    return ESP_ERR_INVALID_ARG;
}