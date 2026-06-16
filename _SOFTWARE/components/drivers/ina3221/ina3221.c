#include "ina3221.h"
#include <string.h>


#define TAG __FILE_NAME__

#define INA3221_I2C_TIMEOUT 40 //ms
#define I2C_FREQ_HZ 100000  // 1000khz possible

#define INA3221_REG_CONFIG                      (0x00)
#define INA3221_REG_SHUNTVOLTAGE_1              (0x01)
#define INA3221_REG_BUSVOLTAGE_1                (0x02)
#define INA3221_REG_CRITICAL_ALERT_1            (0x07)
#define INA3221_REG_WARNING_ALERT_1             (0x08)
#define INA3221_REG_SHUNT_VOLTAGE_SUM           (0x0D)
#define INA3221_REG_SHUNT_VOLTAGE_SUM_LIMIT     (0x0E)
#define INA3221_REG_MASK                        (0x0F)
#define INA3221_REG_VALID_POWER_UPPER_LIMIT     (0x10)
#define INA3221_REG_VALID_POWER_LOWER_LIMIT     (0x11)

void ina3221_task(void *arg);
        
/*************Helper macros ***************************************/
#define RETURN_ON_ERROR(x) do {        \
    esp_err_t __err_rc = (x);          \
    if (__err_rc != ESP_OK) return __err_rc; \
} while (0)

#define CHECK_HANDLE_R(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)

/*************Helper macros ***************************************/

/********************Internal functions ***************************************/

/**Ina3221 doesn't support auto increment */

static esp_err_t _ina3221_read(ina3221_handle_t handle, const uint8_t reg, uint16_t * val)
{
    CHECK_HANDLE_R(val);
    RETURN_ON_ERROR(i2c_master_transmit_receive(handle->i2c_master_dev_handle, (uint8_t[]){reg}, 1, (uint8_t*)val, 2, INA3221_I2C_TIMEOUT));
    *val = (*val >> 8) | (*val << 8);  // Swap
    return ESP_OK;
}

static esp_err_t _ina3221_write(ina3221_handle_t handle, uint8_t reg, uint16_t val)
{
    CHECK_HANDLE_R(handle);
    uint8_t buf[3];
    buf[0] = reg;
    buf[1] = (val >> 8) & 0xFF; 
    buf[2] = val & 0xFF;        

    RETURN_ON_ERROR(i2c_master_transmit(handle->i2c_master_dev_handle, buf, 3, INA3221_I2C_TIMEOUT));
    return ESP_OK;
}

static inline esp_err_t write_config(ina3221_handle_t handle)
{
    return _ina3221_write(handle, INA3221_REG_CONFIG, handle->config.config_register);
}

static inline esp_err_t write_mask(ina3221_handle_t handle)
{
    return _ina3221_write(handle, INA3221_REG_MASK, handle->mask.mask_register & INA3221_MASK_CONFIG);
}

/********************Internal functions ***************************************/

ina3221_handle_t ina3221_new(uint8_t i2c_address)
{
    if (i2c_address < INA3221_I2C_ADDR_GND || i2c_address > INA3221_I2C_ADDR_SCL)
    {
        ESP_LOGE(TAG, "Invalid I2C address, must be between 0x40 and 0x43, provided: 0x%02x", i2c_address);
        return NULL;
    }
    ina3221_handle_t handle = calloc(1, sizeof(_ina3221_data_t));
    if (!handle)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for INA3221 handle");
        return NULL;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_address,
        .scl_speed_hz = I2C_FREQ_HZ
    };

    handle->i2c_device_config = dev_cfg;
    handle->mask.mask_register = INA3221_DEFAULT_MASK;
    handle->config.config_register = INA3221_DEFAULT_CONFIG;
    
    handle->shunt_val_cfg[0] = 10;
    handle->shunt_val_cfg[1] = 10;
    handle->shunt_val_cfg[2] = 10;

    handle->driver_task_handle = NULL;

    if (xTaskCreate(ina3221_task, "ina3221_task", 4096, handle, 5, &handle->driver_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create INA3221 task");
        free(handle);
        return NULL;
    }
    return handle;
}

esp_err_t ina3221_get_status(ina3221_handle_t handle)
{   
    return _ina3221_read(handle, INA3221_REG_MASK, &handle->mask.mask_register);
}

esp_err_t ina3221_set_options(ina3221_handle_t handle, bool bus, bool mode, bool shunt_val_cfg)
{
    handle->config.mode = mode;
    handle->config.ebus = bus;
    handle->config.esht = shunt_val_cfg;
    return write_config(handle);
}

esp_err_t ina3221_enable_channel(ina3221_handle_t handle, bool ch1, bool ch2, bool ch3)
{
    handle->config.ch1 = ch1;
    handle->config.ch2 = ch2;
    handle->config.ch3 = ch3;
    return write_config(handle);
}

void ina3221_set_shunt_resistor(ina3221_handle_t handle, uint16_t resistance_mOhm, ina3221_channel_t channel)
{
    if (channel == INA3221_CHANNEL_ALL)
    {
        handle->shunt_val_cfg[0] = resistance_mOhm;
        handle->shunt_val_cfg[1] = resistance_mOhm;
        handle->shunt_val_cfg[2] = resistance_mOhm;
        return;
    }
    handle->shunt_val_cfg[channel] = resistance_mOhm;
}

esp_err_t ina3221_enable_channel_sum(ina3221_handle_t handle, bool ch1, bool ch2, bool ch3)
{
    handle->mask.scc1 = ch1;
    handle->mask.scc2 = ch2;
    handle->mask.scc3 = ch3;
    return write_mask(handle);
}

esp_err_t ina3221_enable_latch_pin(ina3221_handle_t handle, bool warning, bool critical)
{
    handle->mask.wen = warning;
    handle->mask.cen = critical;
    return write_mask(handle);
}

esp_err_t ina3221_set_average(ina3221_handle_t handle, ina3221_avg_t avg)
{
    handle->config.avg = avg;
    return write_config(handle);
}

esp_err_t ina3221_set_bus_conversion_time(ina3221_handle_t handle, ina3221_ct_t ct)
{
    handle->config.vbus = ct;
    return write_config(handle);
}

esp_err_t ina3221_set_shunt_conversion_time(ina3221_handle_t handle, ina3221_ct_t ct)
{
    handle->config.vsht = ct;
    return write_config(handle);
}

esp_err_t ina3221_reset(ina3221_handle_t handle)
{
    handle->config.config_register = INA3221_DEFAULT_CONFIG;
    
    // FIX: Use the correct default mask
    handle->mask.mask_register = INA3221_DEFAULT_MASK; 
    handle->config.rst = 1;
    
    for (int i = 0; i < 6; i++) {
        handle->user_callback[i] = NULL;
        handle->user_callback_arg[i] = NULL;
    }
    
    esp_err_t err = write_config(handle);
    
    return err;
}

esp_err_t ina3221_update_buses_readings(ina3221_handle_t handle, bool immediate)
{
    if (!immediate) {
        handle->to_update.read_bus_voltage = 1;
        return ESP_OK;
    }

    int16_t raw;
    for (int channel = 0; channel < 3; channel++) {
        RETURN_ON_ERROR(_ina3221_read(handle, INA3221_REG_BUSVOLTAGE_1 + (channel * 2), (uint16_t *)&raw));
        raw = raw >> 3; 
        handle->last_readings.bus_voltage[channel] = raw * 8.0f; // 8mV -> LSB
    }
    return ESP_OK;
}

esp_err_t ina3221_update_shunts_readings(ina3221_handle_t handle, bool immediate)
{
    if(!immediate){
        handle->to_update.read_current = 1;
        return ESP_OK;
    }
    int16_t raw;
    for (int channel = 0; channel < 3; channel++) {
        RETURN_ON_ERROR(_ina3221_read(handle, INA3221_REG_SHUNTVOLTAGE_1 + (channel * 2), (uint16_t *)&raw));

        raw = raw >> 3;
        
        float mvolts = raw * 0.04f;  // 40uV -> LSB
        
        handle->last_readings.shunt_voltage[channel] = mvolts;
        handle->last_readings.shunt_current[channel] = (mvolts * 1000.0f) / handle->shunt_val_cfg[channel]; 
    }
    return ESP_OK;
}

esp_err_t ina3221_get_sum_shunt_value(ina3221_handle_t handle, bool immediate)
{
    if (!immediate) {
        handle->to_update.read_current_sum = 1; 
        return ESP_OK; 
    }

    int16_t raw;
    RETURN_ON_ERROR(_ina3221_read(handle, INA3221_REG_SHUNT_VOLTAGE_SUM, (uint16_t *)&raw));

    raw = raw >> 1; 
    
    handle->last_readings.sum_shunt_voltage = raw * 0.04f; // 40uV -> LSB

    return ESP_OK;
}


esp_err_t ina3221_cfg_periodic_reading(ina3221_handle_t handle, bool bus_voltage, bool current, bool current_sum)
{
    CHECK_HANDLE_R(handle);
    handle->to_update.read_bus_voltage = bus_voltage;
    handle->to_update.read_bus_voltage_periodic = bus_voltage;
    handle->to_update.read_current = current;
    handle->to_update.read_current_periodic = current;
    handle->to_update.read_current_sum = current_sum;
    handle->to_update.read_current_sum_periodic = current_sum;
    return ESP_OK;
}

esp_err_t ina3221_set_alert(ina3221_handle_t handle, ina3221_channel_t channel, int32_t current_mA, bool is_critical)
{
    float limit_mv = ((float)current_mA * handle->shunt_val_cfg[channel]) / 1000.0f;

    int16_t raw_count = (int16_t)(limit_mv / 0.04f); // 40uV -> LSB

    uint16_t reg_val = ((uint16_t)raw_count) << 3; // Shift left by 3 to align with register format// Ensure latches are enabled for alerts
    //RETURN_ON_ERROR(ina3221_enable_latch_pin(handle, true, true));
    uint8_t alert_offset = is_critical ? INA3221_REG_CRITICAL_ALERT_1 : INA3221_REG_WARNING_ALERT_1; // Critical alerts are at odd offsets, warning at even
    return _ina3221_write(handle, alert_offset + channel * 2, reg_val);
}

esp_err_t ina3221_set_sum_warning_alert(ina3221_handle_t handle, uint32_t voltage_mv)
{
    int16_t raw_count = (int16_t)(voltage_mv / 0.04f); // 40uV -> LSB
    uint16_t reg_val = raw_count << 1; // Shift left by 1 to align with register format (Sum register has 15-bit resolution)
    return _ina3221_write(handle, INA3221_REG_SHUNT_VOLTAGE_SUM_LIMIT, reg_val);
}




void ina3221_task(void *arg)
{
    ina3221_handle_t handle = (ina3221_handle_t)arg;
    uint32_t notification_value;

    while (1)
    {
        notification_value = 0;
        xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, portMAX_DELAY);
        if (notification_value == 0) {
            // 1. Read the Mask/Enable register directly into the handle's union
            // This clears the hardware alert latch and populates our bitfields
            if (_ina3221_read(handle, INA3221_REG_MASK, &handle->mask.mask_register) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to read mask register in task");
                goto TASK_END;
            }
            bool crit_flags[3] = {handle->mask.cf & 0x04, handle->mask.cf & 0x02, handle->mask.cf & 0x01};
            bool warn_flags[3] = {handle->mask.wf & 0x04, handle->mask.wf & 0x02, handle->mask.wf & 0x01};
            bool sum_alert     = handle->mask.sf;


            // 3. Use already implemented functions to fetch new data 
            // If ANY channel triggered a warning/critical, we update all basic readings
            esp_err_t err;
            if (handle->mask.cf || handle->mask.wf) {
                err = ina3221_update_buses_readings(handle, true);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to read bus voltages in task");
                    goto TASK_END;
                }
                err = ina3221_update_shunts_readings(handle, true);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to read shunt voltages in task");
                    goto TASK_END;
                }
            }

            // If the sum alert triggered, update the sum specifically
            if (sum_alert) {
                err = ina3221_get_sum_shunt_value(handle, true);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to read sum shunt value in task");
                    goto TASK_END;
                }
            }

            // 4. Handle Callbacks and Logging - invoke per-channel per-alert-type callbacks
            if (handle->alert_critical) {
                ESP_LOGI(TAG, "Critical Alert! CF1:%d CF2:%d CF3:%d", crit_flags[0], crit_flags[1], crit_flags[2]);
                handle->alert_critical = false; 
                // Invoke critical callbacks for each channel that triggered
                for (int ch = 0; ch < 3; ch++) {
                    if (crit_flags[ch]) {
                        int idx = ch * 2 + 1; // Critical callback index for this channel
                        if (handle->user_callback[idx]) {
                            handle->user_callback[idx](handle->user_callback_arg[idx]);
                        }
                    }
                }
            } 
            
            if (handle->alert_warning) {
                ESP_LOGI(TAG, "Warning Alert! WF1:%d WF2:%d WF3:%d SUM:%d", warn_flags[0], warn_flags[1], warn_flags[2], sum_alert);
                handle->alert_warning = false; 
                // Invoke warning callbacks for each channel that triggered
                for (int ch = 0; ch < 3; ch++) {
                    if (warn_flags[ch]) {
                        int idx = ch * 2; // Warning callback index for this channel
                        if (handle->user_callback[idx]) {
                            handle->user_callback[idx](handle->user_callback_arg[idx]);
                        }
                    }
                }
            }
        } 
        else if (notification_value) {
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
            TaskHandle_t caller_task = (TaskHandle_t)(uintptr_t)notification_value;
            #pragma GCC diagnostic pop
            esp_err_t err;
            if (handle->to_update.read_bus_voltage) {
                err = ina3221_update_buses_readings(handle, true);
                if (!handle->to_update.read_bus_voltage_periodic) {
                    handle->to_update.read_bus_voltage = 0;  // Clear flag after execution
                }
            }

            if (handle->to_update.read_current) {
                err = ina3221_update_shunts_readings(handle, true);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to read shunt voltages in task");
                    xTaskNotify(caller_task, 1, eSetBits);
                    goto TASK_END;
                }
                if (!handle->to_update.read_current_periodic) {
                    handle->to_update.read_current = 0;  // Clear flag after execution
                }
            }

            if (handle->to_update.read_current_sum) {
                err = ina3221_get_sum_shunt_value(handle, true);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to read sum shunt value in task");
                    xTaskNotify(caller_task, 1, eSetBits);
                    goto TASK_END;
                }
                if (!handle->to_update.read_current_sum_periodic) {
                    handle->to_update.read_current_sum = 0; // Clear flag after execution
                }
            }
            xTaskNotify(caller_task, 0, eSetBits);
        }
        TASK_END:
        continue;
    }
}

void p_current_monitor_intr_pin_crit_callback(void *arg)
{
    ina3221_handle_t handle = (ina3221_handle_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    handle->alert_critical = true; 
    xTaskNotifyFromISR(handle->driver_task_handle, 0, eSetBits, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
void p_current_monitor_intr_pin_warning_callback(void *arg)
{
    ina3221_handle_t handle = (ina3221_handle_t)arg;  
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    handle->alert_warning = true; 
    xTaskNotifyFromISR(handle->driver_task_handle, 0, eSetBits, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void ina3221_register_user_callback(ina3221_handle_t handle, void (*callback)(void *), void *arg, uint8_t channel, bool is_critical)
{
    if (channel > 2) {
        ESP_LOGE(TAG, "Invalid channel %d, must be 0-2", channel);
        return;
    }
    // Map channel and alert type to callback index: (channel * 2 + (is_critical ? 1 : 0))
    uint8_t idx = channel * 2 + (is_critical ? 1 : 0);
    handle->user_callback[idx] = callback;
    handle->user_callback_arg[idx] = arg;
}
