#include "manager_i2c.h"
#include "rik_onboard_drivers.h"
#include "rik_shared.h"

uint8_t rik_ina_id;
uint8_t rik_tca_id;
uint8_t rik_tps_0_id;
uint8_t rik_tps_1_id;

static tca6424a_handle_t tca6424a_handle = NULL;
static ina3221_handle_t ina3221_handle = NULL;
#define TCA_MOCK

#define TAG __FILE_NAME__
 
status_rep_t rik_i2c_start_tca6424a(uint8_t i2c_addres, bool bus_num){
    esp_err_t res;
    #ifndef TCA_MOCK
        res = m_i2c_device_present(bus_num, i2c_addres);
        if (res != ESP_OK) {
            return STA_C(res, OWN_rik_i2c_start_tca6424a, i2c_addres);
        }
    #endif
    tca6424a_handle = tca_new(i2c_addres);
    if (!tca6424a_handle) return STA_C(ERR_ESP_ERR_NO_MEM, OWN_rik_i2c_start_tca6424a, i2c_addres);
    status_rep_t sta_res = m_i2c_add_driver(bus_num, tca6424a_handle->i2c_dev_config,
        &tca6424a_handle->i2c_dev_handle, (void*)tca6424a_handle,
        tca6424a_handle->task_handle, true, &rik_tca_id);
    if(STA_IS_ERR(sta_res)){
        return STA_PASS_ERR(sta_res);
    }
    res = tca_wrapper_init((void*)tca6424a_handle);
    if (res != ESP_OK) {
        return STA_C(res, OWN_rik_i2c_start_tca6424a, i2c_addres);
    }
    
    ESP_LOGI(TAG, "TCA6424A started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    return STA_OK;
}


status_rep_t rik_i2c_start_ina3221(uint8_t i2c_addres, bool bus_num){
    STA_C_RET_ON_ESP_ERR_PUSH_LOG(m_i2c_device_present(bus_num, i2c_addres),
    OWN_rik_i2c_start_ina3221, i2c_addres);
    
    ESP_LOGI(TAG, "INA3221 detected on bus %d at address 0x%02X", bus_num ? 1 : 0, i2c_addres);

    ina3221_handle = ina3221_new(i2c_addres);

    if (!ina3221_handle) return STA_C(ERR_ESP_ERR_NO_MEM, OWN_rik_i2c_start_ina3221, i2c_addres);

    status_rep_t sta_res = m_i2c_add_driver(bus_num, ina3221_handle->i2c_device_config,
        &ina3221_handle->i2c_master_dev_handle, (void*)ina3221_handle,
        ina3221_handle->driver_task_handle, true, &rik_ina_id);

    if(STA_IS_ERR(sta_res)){
        return STA_PASS_ERR(sta_res);
    }
    sta_res = ina3221_wrapper_init(ina3221_handle);
    if (STA_IS_ERR(sta_res)) {
        return STA_PASS_ERR(sta_res);
    }
    ESP_LOGI(TAG, "INA3221 started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    return STA_OK;
}