#include "manager_i2c.h"
#include "rik_onboard_drivers.h"
#include "rik_shared.h"

#define TCA_MOCK
#define TPS_MOCK

uint8_t rik_ina_id;
uint8_t rik_tca_id;
uint8_t rik_tps_0_id;
uint8_t rik_tps_1_id;

static tca6424a_handle_t tca6424a_handle = NULL;
static ina3221_handle_t ina3221_handle = NULL;
static tps55289_handle_t tps55289_handle_0 = NULL;
static tps55289_handle_t tps55289_handle_1 = NULL;


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
    res = io_sys_tca6424a_init((void*)tca6424a_handle);
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

status_rep_t rik_i2c_start_tsp55289(uint8_t i2c_adders_0, uint8_t i2c_adders_1, bool bus_num){
    #ifndef TPS_MOCK
        esp_err_t res0 = m_i2c_device_present(bus_num, i2c_adders_0);
        esp_err_t res1 = m_i2c_device_present(bus_num, i2c_adders_1);
        if (res0 != ESP_OK) {
            return STA_C(res0, OWN_rik_i2c_start_tsp55289, i2c_adders_0);
        }
        if (res1 != ESP_OK) {
            return STA_C(res1, OWN_rik_i2c_start_tsp55289, i2c_adders_1);
        }
    #endif
    tps55289_handle_0 = tps55289_new(i2c_adders_0);
    if (!tps55289_handle_0) return STA_C(ERR_ESP_ERR_NO_MEM, OWN_rik_i2c_start_tsp55289, i2c_adders_0);
    tps55289_handle_1 = tps55289_new(i2c_adders_1);
    if (!tps55289_handle_1) return STA_C(ERR_ESP_ERR_NO_MEM, OWN_rik_i2c_start_tsp55289, i2c_adders_1);
    
    status_rep_t sta_res = m_i2c_add_driver(bus_num, tps55289_handle_0->i2c_device_config,
        &tps55289_handle_0->i2c_master_dev_handle, (void*)tps55289_handle_0,
        tps55289_handle_0->driver_task_handle, true, &rik_tps_0_id);
    if(STA_IS_ERR(sta_res)){
        return STA_PASS_ERR(sta_res);
    }
    sta_res = m_i2c_add_driver(bus_num, tps55289_handle_1->i2c_device_config,
        &tps55289_handle_1->i2c_master_dev_handle, (void*)tps55289_handle_1,
        tps55289_handle_1->driver_task_handle, true, &rik_tps_1_id);
    if(STA_IS_ERR(sta_res)){
        return STA_PASS_ERR(sta_res);
    }
    ESP_LOGI(TAG, "TPS55289 started on bus %d with addresses 0x%02X and 0x%02X", bus_num ? 1 : 0, i2c_adders_0, i2c_adders_1);
    return STA_OK;
}