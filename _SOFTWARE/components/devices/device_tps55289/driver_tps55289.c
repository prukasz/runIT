#include "driver_tps55289.h"
#include <string.h>
#include "esp_log.h"

static const char * TAG = __FILE_NAME__;



#define CHECK_HANDLE(h) do { if (!(h)) return ESP_ERR_INVALID_ARG; } while(0)

static esp_err_t _tps55289_read(tps55289_handle_t handle, uint8_t reg, uint8_t *data, size_t len)
{
    if (!handle->header.transmit_receive) return ESP_ERR_INVALID_STATE;
    esp_err_t err = handle->header.transmit_receive(handle, &reg, 1, data, len);
    if (err == ESP_OK) {
        for (size_t i = 0; i < len; i++) {
            if ((reg + i) < 8) {
                handle->reg_cache[reg + i] = data[i];
            }
        }
    }
    return err;
}

static esp_err_t _tps55289_write(tps55289_handle_t handle, uint8_t reg, const uint8_t *data, size_t len)
{
    if (!handle->header.transmit) return ESP_ERR_INVALID_STATE;
    uint8_t buf[9];
    if (len > 8) return ESP_ERR_INVALID_ARG;
    buf[0] = reg;
    memcpy(&buf[1], data, len);

    esp_err_t err = handle->header.transmit(handle, buf, len + 1);
    if (err == ESP_OK) {
        for (size_t i = 0; i < len; i++) {
            if ((reg + i) < 8) {
                handle->reg_cache[reg + i] = data[i];
            }
        }
    }
    return err;
}

static void _tps55289_parse_status(tps55289_handle_t handle, uint8_t raw_status)
{
    handle->last_status.raw_status_reg = raw_status;
    handle->last_status.scp = (raw_status & 0x80) != 0;
    handle->last_status.ocp = (raw_status & 0x40) != 0;
    handle->last_status.ovp = (raw_status & 0x20) != 0;
    handle->last_status.op_mode = (raw_status & 0x03);
}

void tps55289_driver_task(void *arg)
{
    tps55289_handle_t handle = (tps55289_handle_t)arg;
    uint32_t notification_value;

    while (1) {
        xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, portMAX_DELAY);
        
        uint8_t raw_status = 0;
        if (_tps55289_read(handle, TPS55289_REG_STATUS, &raw_status, 1) == ESP_OK) {
            _tps55289_parse_status(handle, raw_status);

            if ((handle->last_status.ovp || handle->last_status.ocp || handle->last_status.scp) && handle->on_fault_cb) {
                handle->on_fault_cb(handle->on_fault_arg, handle->last_status.ovp, handle->last_status.ocp, handle->last_status.scp);
            }
        }
    }
}

tps55289_handle_t tps55289_new(uint8_t i2c_address, bool i2c_bus_num)
{
    if (i2c_address != TPS55289_I2C_ADDR_74 && i2c_address != TPS55289_I2C_ADDR_75) {
        ESP_LOGE(TAG, "Invalid I2C address for TPS55289: 0x%02X", i2c_address);
        return NULL;
    }

    tps55289_handle_t handle = calloc(1, sizeof(_tps55289_data_t));
    if (!handle) return NULL;

    handle->shunt_resistor_mohm = 10;

    // Initialize I2C device config structure
    handle->header.i2c_device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    handle->header.i2c_device_config.device_address = i2c_address;
    handle->header.i2c_device_config.scl_speed_hz = 400000;
    handle->header.bus_num = i2c_bus_num;

    // Create RTOS Task
    if (xTaskCreate(tps55289_driver_task, "tps55289_task", 2048, handle, 5, &handle->driver_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TPS55289 task");
        free(handle);
        return NULL;
    }

    return handle;
}

void tps55289_delete(tps55289_handle_t handle)
{
    if (handle) {
        if (handle->driver_task) {
            vTaskDelete(handle->driver_task);
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


esp_err_t tps55289_set_output_enable(tps55289_handle_t handle, bool enable)
{
    CHECK_HANDLE(handle);
    uint8_t mode = 0;
    _tps55289_read(handle, TPS55289_REG_MODE, &mode, 1);
    if (enable) mode |= 0x80;
    else mode &= ~0x80;
    return _tps55289_write(handle, TPS55289_REG_MODE, &mode, 1);
}

esp_err_t tps55289_set_current_limit(tps55289_handle_t handle, bool enable, uint16_t limit_ma)
{
    CHECK_HANDLE(handle);
    float target_v_ilim_mv = (limit_ma * handle->shunt_resistor_mohm) / 1000.0f;
    if (target_v_ilim_mv > 63.5f) target_v_ilim_mv = 63.5f;

    uint8_t reg_val = (uint8_t)(target_v_ilim_mv / 0.5f);
    if (reg_val > 127) reg_val = 127;

    uint8_t final_reg_data = (enable ? 0x80 : 0x00) | (reg_val & 0x7F);
    return _tps55289_write(handle, TPS55289_REG_IOUT_LIMIT, &final_reg_data, 1);
}

esp_err_t tps55289_set_voltage(tps55289_handle_t handle, uint16_t voltage_mv)
{
    CHECK_HANDLE(handle);
    uint8_t vout_fs = 0;
    _tps55289_read(handle, TPS55289_REG_VOUT_FS, &vout_fs, 1);
    bool is_external_fb = (vout_fs & 0x80) != 0;

    uint16_t ref_val = 0;
    if (is_external_fb) {
        if (voltage_mv < 800) voltage_mv = 800;
        ref_val = (voltage_mv - 800) / 10;
    } else {
        uint8_t intfb = vout_fs & 0x03;
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

    if (ref_val > 0x07FF) ref_val = 0x07FF;

    uint8_t lsb = ref_val & 0xFF;
    uint8_t msb = (ref_val >> 8) & 0xFF;
    uint8_t buf[2] = { lsb, msb };
    return _tps55289_write(handle, TPS55289_REG_REF_LSB, buf, 2);
}

esp_err_t tps55289_set_mode(tps55289_handle_t handle, bool fpwm, bool hiccup)
{
    CHECK_HANDLE(handle);
    uint8_t mode = 0;
    _tps55289_read(handle, TPS55289_REG_MODE, &mode, 1);
    if (fpwm) mode |= 0x02; else mode &= ~0x02;
    if (hiccup) mode |= 0x20; else mode &= ~0x20;
    return _tps55289_write(handle, TPS55289_REG_MODE, &mode, 1);
}

esp_err_t tps55289_set_fault_masks(tps55289_handle_t handle, bool mask_scp, bool mask_ocp, bool mask_ovp)
{
    CHECK_HANDLE(handle);
    uint8_t cdc = 0;
    _tps55289_read(handle, TPS55289_REG_CDC, &cdc, 1);
    if (mask_scp) cdc |= 0x80; else cdc &= ~0x80;
    if (mask_ocp) cdc |= 0x40; else cdc &= ~0x40;
    if (mask_ovp) cdc |= 0x20; else cdc &= ~0x20;
    return _tps55289_write(handle, TPS55289_REG_CDC, &cdc, 1);
}

void tps55289_register_on_fault_callback(tps55289_handle_t handle, void (*callback)(void *, bool, bool, bool), void* arg)
{
    if (handle) {
        handle->on_fault_cb = callback;
        handle->on_fault_arg = arg;
    }
}


void IRAM_ATTR tps55289_isr_handler(tps55289_handle_t handle)
{
    if (handle && handle->driver_task) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xTaskNotifyFromISR(handle->driver_task, 0, eNoAction, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}