#include "provider_adc_expander.h"
#include "manager_io.h"
#include "ads7128.h"


static ads_handle_t _ads_handle = NULL;
static bool _freeze = false;

#define CHECK_HANDLE(VAL) do { if (!(VAL)) return STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_PROVIDER_ADC_EXPANDER, 0); } while (0)
#define CHECK_CHANNEL(CHANNEL) do { if ((CHANNEL) > 2) return STA_C(PWR_ERR_INVALID_PARAM, OWNER_PROVIDER_ADC_EXPANDER, (CHANNEL)); } while (0)


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

void p_adc_expander_freeze(bool freeze){
    _freeze = freeze;
}

status_rep_t p_adc_expander_read_voltage(uint64_t pin_mask, uint32_t* out_mv, uint8_t max_results_num)
{
    CHECK_HANDLE(_ads_handle);
    ads_analog_ch_read(_ads_handle, (uint8_t)pin_mask, !_freeze);
    
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

status_rep_t p_adc_expander_register_callback(uint8_t pin, void* adc_int_config)
{
    CHECK_HANDLE(_ads_handle);
    sys_io_adc_int_config_t* config = (sys_io_adc_int_config_t*)adc_int_config;
    uint8_t channel = pin+1;
    uint16_t h_thres = clamp_uint16((uint16_t)(config->adc_threshold_up_mv)/(ratio), 0, 4095);
    uint16_t l_thres = clamp_uint16((uint16_t)(config->adc_threshold_down_mv)/(ratio), 0, 4095);
    uint8_t hist = clamp_uint8((uint8_t)(config->adc_threshold_hysteresis_mv)/(ratio*8), 0, 15);
    uint8_t event_cnt = clamp_uint8((uint8_t)(config->adc_event_counter_threshold-1), 0, 15);
    
    h_thres = h_thres | hist<<4;
    l_thres = l_thres | event_cnt<<4;

    esp_err_t ret = ads_set_alert_cfg(_ads_handle, channel, h_thres, l_thres, config->adc_window_mode, 1, !_freeze);

    if(ret != ESP_OK){
        return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_ADC_EXPANDER, ret);
    }

    ret = ads_register_alert_callback(_ads_handle, 1<<(channel-1), config->callback, config->arg);

    if(ret != ESP_OK){
        return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_ADC_EXPANDER, ret);
    }

    return STA_OK;
}

status_rep_t p_adc_expander_reset_all(void) {
    CHECK_HANDLE(_ads_handle);
    /* Reset all 8 ADC channels: clear thresholds, hysteresis, event counters, and disable alerts */
    for (uint8_t ch = 0; ch < 8; ch++) {
        /* Clear all alert configuration for this channel */
        _ads_handle->alert_configs[ch].h_thres_msb = 0;
        _ads_handle->alert_configs[ch].histeresis_config.h_thres_lsb = 0;
        _ads_handle->alert_configs[ch].histeresis_config.hist = 0;
        _ads_handle->alert_configs[ch].l_thres_msb = 0;
        _ads_handle->alert_configs[ch].event_count_config.l_thres_lsb = 0;
        _ads_handle->alert_configs[ch].event_count_config.event_cnt = 0;
        _ads_handle->alert_configs[ch].mode = 0;
        _ads_handle->alert_configs[ch].route_to_alert_pin = false;
        
        /* Disable alert on hardware (set all thresholds to 0, window mode OUTSIDE) */
        esp_err_t ret = ads_set_alert_cfg(_ads_handle, ch + 1, 0, 0, 0, 0, true);
        if (ret != ESP_OK) {
            return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_ADC_EXPANDER, ret);
        }
        
        /* Clear callbacks and arguments */
        _ads_handle->callbacks[ch] = NULL;
        _ads_handle->callback_args[ch] = NULL;
    }
    
    /* Clear alert triggered flag */
    _ads_handle->alert_triggered = false;
    
    return STA_OK;
}

void * p_adc_expander_new_handle(uint8_t i2c_addr){
    _ads_handle = ads_new(i2c_addr);
    return (void*)_ads_handle;
}

i2c_master_dev_handle_t *p_adc_expander_get_i2c_dev_handle(){
    return &_ads_handle->i2c_dev_handle;
}

TaskHandle_t p_adc_expander_get_task_handle(){
    return _ads_handle->task_handle;
}

i2c_device_config_t* p_adc_expander_get_i2c_dev_config(){
    return &_ads_handle->i2c_dev_config;
}

