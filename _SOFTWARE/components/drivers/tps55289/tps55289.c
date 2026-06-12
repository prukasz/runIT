#include "tps55289.h"
#include <string.h>
#include "esp_log.h"
#include "tps55289_mock.h"

#define TAG __FILE_NAME__

#define TPS55289_I2C_TIMEOUT 20 // ms
#define I2C_FREQ_HZ 100000      // 400kHz

/************* Helper macros ***************************************/
#define RETURN_ON_ERROR(x) do {        \
    esp_err_t __err_rc = (x);          \
    if (__err_rc != ESP_OK) return __err_rc; \
} while (0)

#define CHECK_HANDLE_R(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)
/************* Helper macros ***************************************/

/******************** Internal functions ***************************************/

static esp_err_t _tps55289_read(tps55289_handle_t handle, const uint8_t reg, uint8_t *val)
{
    CHECK_HANDLE_R(val);
    CHECK_HANDLE_R(handle);
    
    uint8_t i2c_addr = handle->i2c_device_config.device_address;

    #if CONFIG_USE_MOCK_TPS55289
    RETURN_ON_ERROR(tps_transmit_receive(i2c_addr, (uint8_t[]){reg}, 1, val, 1, TPS55289_I2C_TIMEOUT));
    #else 
    RETURN_ON_ERROR(i2c_master_transmit_receive(handle->i2c_master_dev_handle, (uint8_t[]){reg}, 1, val, 1, TPS55289_I2C_TIMEOUT));
    #endif

    if (reg < TPS55289_REG_MAX) {
        handle->reg_cache[reg] = *val;
    }
    
    return ESP_OK;
}

static esp_err_t _tps55289_write(tps55289_handle_t handle, uint8_t reg, uint8_t val)
{
    CHECK_HANDLE_R(handle);
    uint8_t buf[2] = { reg, val };

    uint8_t i2c_addr = handle->i2c_device_config.device_address;
    
    #if CONFIG_USE_MOCK_TPS55289
    RETURN_ON_ERROR(tps_transmit(i2c_addr, buf, 2, TPS55289_I2C_TIMEOUT));
    #else
    RETURN_ON_ERROR(i2c_master_transmit(handle->i2c_master_dev_handle, buf, 2, TPS55289_I2C_TIMEOUT));
    #endif
    
    if (reg < TPS55289_REG_MAX) {
        handle->reg_cache[reg] = val;
    }
    
    return ESP_OK;
}

static void _tps55289_parse_status(tps55289_handle_t handle, uint8_t raw_status)
{
    handle->last_status.raw_status_reg = raw_status;
    handle->last_status.scp = (raw_status & 0x80) ? true : false;
    handle->last_status.ocp = (raw_status & 0x40) ? true : false;
    handle->last_status.ovp = (raw_status & 0x20) ? true : false;
    handle->last_status.op_mode = (raw_status & 0x03);
}

/******************** API functions ***************************************/

void tps55289_delete(tps55289_handle_t handle)
{
    if (handle) {
        if (handle->driver_task_handle) {
            vTaskDelete(handle->driver_task_handle);
        }
        free(handle);
    }
}

void tps55289_set_shunt_resistor(tps55289_handle_t handle, uint16_t resistance_mOhm)
{
    if (handle) {
        handle->shunt_resistor_mohm = resistance_mOhm;
    }
}

esp_err_t tps55289_sync_all_registers(tps55289_handle_t handle)
{
    CHECK_HANDLE_R(handle);
    uint8_t temp;
    for (uint8_t reg = 0; reg < TPS55289_REG_MAX; reg++) {
        RETURN_ON_ERROR(_tps55289_read(handle, reg, &temp));
    }
    return ESP_OK;
}


esp_err_t tps55289_set_output_enable(tps55289_handle_t handle, bool enable)
{
    CHECK_HANDLE_R(handle);
    uint8_t mode;
    RETURN_ON_ERROR(_tps55289_read(handle, TPS55289_REG_MODE, &mode));
    if (enable) mode |= 0x80; // Bit 7: OE
    else mode &= ~0x80;
    return _tps55289_write(handle, TPS55289_REG_MODE, mode);
}

esp_err_t tps55289_set_current_limit(tps55289_handle_t handle, bool enable, uint16_t limit_ma)
{
    CHECK_HANDLE_R(handle);
    
    float target_v_ilim_mv = (limit_ma * handle->shunt_resistor_mohm) / 1000.0f;
    
    // Maksymalne napięcie na boczniku dla TPS55289 to ok 63.5mV
    if (target_v_ilim_mv > 63.5f) {
        target_v_ilim_mv = 63.5f;
    }
    
    // Krok ustawienia IOUT_LIMIT to 0.5mV
    uint8_t reg_val = (uint8_t)(target_v_ilim_mv / 0.5f);
    
    // Zabezpieczenie przed przepełnieniem (max wartość bitowa 7-bit to 127)
    if (reg_val > 127) {
        reg_val = 127;
    }

    uint8_t final_reg_data = (enable ? 0x80 : 0x00) | (reg_val & 0x7F);
    
    ESP_LOGI(TAG, "Set Current Limit: %d mA -> %f mV across %d mOhm shunt. (Reg val: 0x%02X)", 
             limit_ma, target_v_ilim_mv, handle->shunt_resistor_mohm, final_reg_data);

    return _tps55289_write(handle, TPS55289_REG_IOUT_LIMIT, final_reg_data);
}

esp_err_t tps55289_set_voltage(tps55289_handle_t handle, uint16_t voltage_mv)
{
    CHECK_HANDLE_R(handle);
    
    uint8_t vout_fs;
    RETURN_ON_ERROR(_tps55289_read(handle, TPS55289_REG_VOUT_FS, &vout_fs));
    bool is_external_fb = (vout_fs & 0x80) != 0; // Bit 7: FB
    
    uint16_t ref_val = 0;
    
    if (is_external_fb) {
        // Fallback for external divider, assuming 10mV/stepp default
        ESP_LOGW(TAG, "External FB used, assuming 10mV step");
        if (voltage_mv < 800) voltage_mv = 800;
        ref_val = (voltage_mv - 800) / 10;
    } else {
        uint8_t intfb = vout_fs & 0x03; // Bits [1:0]: INTFB
        float step_mv = 10.0f;
        uint16_t min_vout_mv = 800;
        
        switch (intfb) {
            case 0x00: step_mv = 2.5f; min_vout_mv = 200; break;
            case 0x01: step_mv = 5.0f; min_vout_mv = 400; break;
            case 0x02: step_mv = 7.5f; min_vout_mv = 600; break;
            case 0x03: step_mv = 10.0f; min_vout_mv = 800; break;
        }
        
        if (voltage_mv < min_vout_mv) voltage_mv = min_vout_mv;
        ref_val = (uint16_t)((voltage_mv - min_vout_mv) / step_mv);
    }
    
    // Protect 11-bit DAC limit (0x07FF = 2047)
    if (ref_val > 0x07FF) {
        ref_val = 0x07FF;
        ESP_LOGW(TAG, "Voltage request exceeded maximum DAC value, clamping to 0x07FF.");
    }
    
    // Wpisuje 11-bitową wartość LSB i MSB
    uint8_t lsb = ref_val & 0xFF;
    uint8_t msb = (ref_val >> 8) & 0xFF;

    RETURN_ON_ERROR(_tps55289_write(handle, TPS55289_REG_REF_LSB, lsb));
    RETURN_ON_ERROR(_tps55289_write(handle, TPS55289_REG_REF_MSB, msb));
    return ESP_OK;
}

esp_err_t tps55289_set_mode(tps55289_handle_t handle, bool fpwm, bool hiccup)
{
    CHECK_HANDLE_R(handle);
    uint8_t mode;
    RETURN_ON_ERROR(_tps55289_read(handle, TPS55289_REG_MODE, &mode));
    
    if (fpwm) mode |= 0x10; // Bit 4: FPWM (1) / Auto PFM (0)
    else mode &= ~0x10;
    
    if (hiccup) mode |= 0x20; // Bit 5: Hiccup (1) / Latch-off (0)
    else mode &= ~0x20;

    return _tps55289_write(handle, TPS55289_REG_MODE, mode);
}

esp_err_t tps55289_set_fault_masks(tps55289_handle_t handle, bool mask_scp, bool mask_ocp, bool mask_ovp)
{
    CHECK_HANDLE_R(handle);
    uint8_t cdc;
    RETURN_ON_ERROR(_tps55289_read(handle, TPS55289_REG_CDC, &cdc));

    if (mask_scp) cdc |= 0x80; else cdc &= ~0x80;
    if (mask_ocp) cdc |= 0x40; else cdc &= ~0x40;
    if (mask_ovp) cdc |= 0x20; else cdc &= ~0x20;

    return _tps55289_write(handle, TPS55289_REG_CDC, cdc);
}

// --- --- ---

esp_err_t tps55289_get_status(tps55289_handle_t handle)
{   
    CHECK_HANDLE_R(handle);

    uint8_t raw;
    esp_err_t err = _tps55289_read(handle, TPS55289_REG_STATUS, &raw);
    if (err == ESP_OK) {
        _tps55289_parse_status(handle, raw);
    }
    return err;
}

void tps55289_register_user_callback(tps55289_handle_t handle, tps55289_fault_type_t type, void (*callback)(void *), void *arg)
{
    switch(type) {
        case TPS55289_FAULT_OVP:
            handle->user_callback_ovp = callback;
            handle->user_callback_arg_ovp = arg;
            break;
        case TPS55289_FAULT_OCP:
            handle->user_callback_ocp = callback;
            handle->user_callback_arg_ocp = arg;
            break;
        case TPS55289_FAULT_SCP:
            handle->user_callback_scp = callback;
            handle->user_callback_arg_scp = arg;
            break;
        default:
            break;
    }
}

void tps55289_task(void *arg)
{
    tps55289_handle_t handle = (tps55289_handle_t)arg;
    uint32_t notification_value;

    while (1)
    {
        notification_value = 0;
        xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, portMAX_DELAY);
        
        if (notification_value == 0) {
            uint8_t raw_status;
            esp_err_t err = _tps55289_read(handle, TPS55289_REG_STATUS, &raw_status); 
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to read status register in interrupt handler");
                goto TASK_END;
            }
            _tps55289_parse_status(handle, raw_status);

            if (handle->alert_fault) {
                ESP_LOGW(TAG, "[0x%02X] Fault Alert! SCP:%d OCP:%d OVP:%d", 
                         handle->i2c_device_config.device_address,
                         handle->last_status.scp, handle->last_status.ocp, handle->last_status.ovp);
                
                handle->alert_fault = false; 
                
                if (handle->last_status.ovp && handle->user_callback_ovp) {
                    handle->user_callback_ovp(handle->user_callback_arg_ovp);
                }
                
                if (handle->last_status.ocp && handle->user_callback_ocp) {
                    handle->user_callback_ocp(handle->user_callback_arg_ocp);
                }

                if (handle->last_status.scp && handle->user_callback_scp) {
                    handle->user_callback_scp(handle->user_callback_arg_scp);
                }
            } 
        } 
        else {
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
            TaskHandle_t caller_task = (TaskHandle_t)(uintptr_t)notification_value;
            #pragma GCC diagnostic pop
            
            if (caller_task) {
                xTaskNotify(caller_task, 0, eSetBits);
            }
        }
        TASK_END:
        continue;
    }
}

IRAM_ATTR void p_vreg_intr_pin_fault_callback(void *arg)
{
    tps55289_handle_t handle = (tps55289_handle_t)arg;
    if (!handle) return;
    
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    handle->alert_fault = true; 
    
    xTaskNotifyFromISR(handle->driver_task_handle, 0, eSetBits, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

tps55289_handle_t tps55289_new(uint8_t i2c_address)
{
    tps55289_handle_t handle = calloc(1, sizeof(_tps55289_data_t));
    if (!handle)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for TPS55289 handle");
        return NULL;
    }


    if (i2c_address != TPS55289_I2C_ADDR_74 && i2c_address != TPS55289_I2C_ADDR_75)
    {
        ESP_LOGW(TAG, "Unusual I2C address, typically 0x74 or 0x75, provided: 0x%02x", i2c_address);
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_address,
        .scl_speed_hz = I2C_FREQ_HZ
    };

    handle->i2c_device_config = dev_cfg;
    handle->shunt_resistor_mohm = 10; 
    handle->driver_task_handle = NULL;
    
    if (xTaskCreate(tps55289_task, NULL, 4096, handle, 5, &handle->driver_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TPS55289 task");
        free(handle);
        return NULL;
    }
    
    return handle;
}