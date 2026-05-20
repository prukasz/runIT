#include "provider_adc.h"
#include "ads7128.h"

static ads_handle_t _ads_handle = NULL;

static bool _dereffered_mode = false;

typedef struct{
    uint32_t adc_threshold_up_mv;
    uint32_t adc_threshold_down_mv;
    uint32_t adc_threshold_hysteresis_mv;
    uint32_t adc_event_counter_threshold;
    uint32_t adc_window_mode; //0: outside window, 1: inside window
    //add here window type 
    void (*callback)(void* arg);
    void* arg;
}sys_io_adc_int_config_t;

static inline uint16_t clamp_uint16(uint16_t val, uint16_t min, uint16_t max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

static inline uint8_t clamp_uint8(uint8_t val, uint8_t min, uint8_t max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

void _sys_adc_expander_delay_updates(bool dereffered_mode){
    _dereffered_mode = dereffered_mode;
}

status_rep_t _sys_adc_expander_read(uint64_t pin_mask, uint32_t* out_mv, uint8_t max_results_num)
{
    ads_analog_ch_read(_ads_handle, (uint8_t)pin_mask, !_dereffered_mode);
    
    uint8_t pos = 0;
    
    for(uint8_t i = 0; i < 8; i++){
        if((uint8_t)pin_mask & (1 << i)){
            if(pos >= max_results_num){
                break;
            }
            uint16_t raw = _ads_handle->recent_analog_values[i];
            out_mv[pos] = (uint32_t)(raw * ratio);
            pos++;
        }
    }
    return STA_OK;
}

status_rep_t _sys_adc_expander_register_callback(uint64_t pin_mask, void* adc_int_config)
{
    sys_io_adc_int_config_t* config = (sys_io_adc_int_config_t*)adc_int_config;
    uint8_t channel = __builtin_ctz(pin_mask) + 1;
    uint16_t h_thres = clamp_uint16((uint16_t)(config->adc_threshold_up_mv)/(ratio), 0, 4095);
    uint16_t l_thres = clamp_uint16((uint16_t)(config->adc_threshold_down_mv)/(ratio), 0, 4095);
    uint8_t hist = clamp_uint8((uint8_t)(config->adc_threshold_hysteresis_mv)/(ratio*8), 0, 15);
    uint8_t event_cnt = clamp_uint8((uint8_t)(config->adc_event_counter_threshold-1), 0, 15);
    
    h_thres = h_thres | hist<<4;
    l_thres = l_thres | event_cnt<<4;

    esp_err_t ret = ads_set_alert_cfg(_ads_handle, channel, h_thres, l_thres, config->adc_window_mode, 1, !_dereffered_mode);

    if(ret != ESP_OK){
        return STA_OK;
    }

    ret = ads_register_alert_callback(_ads_handle, 1<<(channel-1), config->callback, config->arg);

    if(ret != ESP_OK){
        return STA_OK;
    }

    return STA_OK;
}

void * provider_adc_expander_new_handle(uint8_t i2c_addr){
    _ads_handle = ads_new(i2c_addr);
    return (void*)_ads_handle;
}

i2c_master_dev_handle_t *provider_adc_expander_get_i2c_dev_handle(){
    return &_ads_handle->i2c_dev_handle;
}

TaskHandle_t provider_adc_expander_get_task_handle(){
    return _ads_handle->task_handle;
}

i2c_device_config_t* provider_adc_expander_get_i2c_dev_config(){
    return &_ads_handle->i2c_dev_config;
}
