#include "provider_power_delivery.h"
#include "esp_log.h"
#include "ap33772s.h"

#define TAG __FILE_NAME__

#undef OWNER
#define OWNER OWNER_PROVIDER_POWER_DELIVERY

static ap33772s_handle_t pd_handle = NULL;

void* p_power_delivery_new(void) {
    pd_handle = ap33772s_new();
    return (void*)pd_handle;
}

i2c_device_config_t* p_power_delivery_get_i2c_dev_config(void) {
    if (pd_handle) return &pd_handle->i2c_device_config;
    return NULL;
}

i2c_master_dev_handle_t* p_power_delivery_get_i2c_dev_handle(void) {
    if (pd_handle) return &pd_handle->i2c_master_dev_handle;
    return NULL;
}

TaskHandle_t p_power_delivery_get_task_handle(void) {
    if (pd_handle) return pd_handle->driver_task_handle;
    return NULL;
}

status_rep_t p_power_delivery_begin(void) {
    CHECK_HANDLE_R(pd_handle);
    esp_err_t err = ap33772s_begin(pd_handle);
    return STA_FROM_ESP(err);
}

status_rep_t p_power_delivery_set_voltage_and_current(uint32_t voltage_mv, uint32_t current_ma) {
    CHECK_HANDLE_R(pd_handle);

    // 1. Try Programmable Power Supply (PPS) dynamic rail
    if (pd_handle->index_pps_user != -1) {
        src_spr_and_epr_pdo_fields_t active_pdo = pd_handle->src_pdo_array[pd_handle->index_pps_user - 1];
        int voltage_min_decoded = (active_pdo.pps.voltage_min > 0) ? 3300 : 0;
        int voltage_max_decoded = active_pdo.pps.voltage_max * 100;
        
        if (voltage_mv >= voltage_min_decoded && voltage_mv <= voltage_max_decoded) {
            esp_err_t err = ap33772s_set_pps_pdo(pd_handle, pd_handle->index_pps_user, voltage_mv, current_ma);
            if (err == ESP_OK) return STA_OK;
        }
    }
    
    // 2. Try Adjustable Voltage Supply (AVS) profile
    if (pd_handle->index_avs_user != -1) {
        src_spr_and_epr_pdo_fields_t active_pdo = pd_handle->src_pdo_array[pd_handle->index_avs_user - 1];
        int voltage_min_decoded = (active_pdo.avs.voltage_min > 0) ? 15000 : 0;
        int voltage_max_decoded = active_pdo.avs.voltage_max * 200;

        if (voltage_mv >= voltage_min_decoded && voltage_mv <= voltage_max_decoded) {
            esp_err_t err = ap33772s_set_avs_pdo(pd_handle, pd_handle->index_avs_user, voltage_mv, current_ma);
            if (err == ESP_OK) return STA_OK;
        }
    }

    // 3. Fallback to Fixed - find the nearest lower or equal voltage
    int best_pdo_index = -1;
    int best_voltage_diff = 1000000;

    for (int i = 1; i <= MAX_PDO_ENTRIES; i++) {
        src_spr_and_epr_pdo_fields_t pdo = pd_handle->src_pdo_array[i - 1];
        if (pdo.fixed.type == 0 && (pdo.byte0 != 0 || pdo.byte1 != 0)) {
            bool isEPR = (i >= 8);
            int pdo_volt_mv = pdo.fixed.voltage_max * (isEPR ? 200 : 100);
            
            if (pdo_volt_mv <= voltage_mv) {
                int diff = voltage_mv - pdo_volt_mv;
                if (diff < best_voltage_diff) {
                    best_voltage_diff = diff;
                    best_pdo_index = i;
                }
            }
        }
    }

    if (best_pdo_index != -1) {
        esp_err_t err = ap33772s_set_fixed_pdo(pd_handle, best_pdo_index, current_ma);
        if (err == ESP_OK) return STA_OK;
        return STA_FROM_ESP(err);
    }

    return STA_C(ERR_INVALID_ARG, OWNER, voltage_mv);
}

status_rep_t p_power_delivery_get_voltage(uint32_t *voltage_mv) {
    CHECK_HANDLE_R(pd_handle);
    CHECK_NOT_NULL_R(voltage_mv);
    int vol = ap33772s_read_voltage(pd_handle);
    if (vol < 0) return STA_C(PWR_ERR_UPDATE_FAILED, OWNER, 0);
    *voltage_mv = vol;
    return STA_OK;
}

status_rep_t p_power_delivery_get_current(int32_t *current_ma) {
    CHECK_HANDLE_R(pd_handle);
    CHECK_NOT_NULL_R(current_ma);
    int curr = ap33772s_read_current(pd_handle);
    if (curr < 0) return STA_C(PWR_ERR_UPDATE_FAILED, OWNER, 0);
    *current_ma = curr;
    return STA_OK;
}