#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "rtos_utils.h"
#include "manager_io.h"
#include "esp_adc_config.h"

#define TAG "ADC_DMA"

#define ADC_SAMPLE_FREQ_HZ 2000
#define ADC_FRAME_SIZE_BYTES 80
#define IIR_ALPHA 0.01f

R_MUTEX_DEFINE(adc_mutex);
R_TASK_DEFINE(adc_processing_task, 4096);

typedef struct {
    uint16_t adc_last_read_mv;
    uint16_t adc_cached_mv;
    uint16_t adc_treshold_h_mv;
    uint16_t adc_treshold_l_mv;
    uint16_t hysteresis_mv;
    void (*callback)(void* arg);
    void* callback_arg;
    uint8_t window_type;
    bool alert_was_triggered;
} esp_adc_pin_config_t;

static esp_adc_pin_config_t* adc_pin_configs[10] = {0};
static adc_continuous_handle_t adc_handle = NULL;
static adc_cali_handle_t cali_handles[10] = {NULL};
static float internal_raw_filtered[10] = {0};
static uint16_t last_channels_mask = 0;
static bool is_adc_running = false;
static bool is_frozen = false;

static esp_err_t configure_adc_pattern(uint16_t channel_mask);

esp_err_t esp_adc_add_pin(uint8_t channel, void* sys_io_adc_int_config)
{
    sys_io_adc_int_config_t* adc_pin_cfg = (sys_io_adc_int_config_t*)sys_io_adc_int_config;
    if (channel >= 10 || adc_pin_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;
    if (R_MUTEX_LOCK(adc_mutex, WAIT_FOREVER) != pdTRUE) {
        return ESP_FAIL;
    }

    esp_adc_pin_config_t* pin_cfg_handle = adc_pin_configs[channel];
    if (pin_cfg_handle != NULL) {
        ESP_LOGI(TAG, "Updating active adc pin configuration for channel %d", channel);
        pin_cfg_handle->adc_treshold_h_mv = adc_pin_cfg->adc_threshold_up_mv;
        pin_cfg_handle->adc_treshold_l_mv = adc_pin_cfg->adc_threshold_down_mv;
        pin_cfg_handle->hysteresis_mv = adc_pin_cfg->adc_threshold_hysteresis_mv;
        pin_cfg_handle->callback = adc_pin_cfg->callback;
        pin_cfg_handle->callback_arg = adc_pin_cfg->arg;
        pin_cfg_handle->window_type = (uint8_t)adc_pin_cfg->adc_window_mode;
    } else {
        pin_cfg_handle = calloc(1, sizeof(esp_adc_pin_config_t));
        if (pin_cfg_handle == NULL) {
            err = ESP_ERR_NO_MEM;
            goto exit;
        }

        pin_cfg_handle->adc_treshold_h_mv = adc_pin_cfg->adc_threshold_up_mv;
        pin_cfg_handle->adc_treshold_l_mv = adc_pin_cfg->adc_threshold_down_mv;
        pin_cfg_handle->hysteresis_mv = adc_pin_cfg->adc_threshold_hysteresis_mv;
        pin_cfg_handle->callback = adc_pin_cfg->callback;
        pin_cfg_handle->callback_arg = adc_pin_cfg->arg;
        pin_cfg_handle->window_type = (uint8_t)adc_pin_cfg->adc_window_mode;// Initialize unfrozen
        adc_pin_configs[channel] = pin_cfg_handle;
    }

    last_channels_mask |= (1U << channel);

    if (is_adc_running) {
        err = adc_continuous_stop(adc_handle);
        if (err != ESP_OK) {
            goto exit;
        }
    }

    err = configure_adc_pattern(last_channels_mask);
    if (err != ESP_OK) {
        goto exit;
    }

    err = adc_continuous_start(adc_handle);
    if (err != ESP_OK) {
        goto exit;
    }

    is_adc_running = true;

exit:
    R_MUTEX_UNLOCK(adc_mutex);
    return err;
}

// --- NEW: Function to freeze/unfreeze caching on a channel ---
void esp_adc_freeze(bool freeze)
{
    is_frozen = freeze;
}

static esp_err_t configure_adc_pattern(uint16_t channel_mask)
{
    adc_digi_pattern_config_t adc_pattern[10];
    int pattern_count = 0;

    for (int i = 0; i < 10; i++) {
        if (channel_mask & (1U << i)) {
            adc_pattern[pattern_count].atten = ADC_ATTEN_DB_12;
            adc_pattern[pattern_count].channel = i;
            adc_pattern[pattern_count].unit = ADC_UNIT_1;
            adc_pattern[pattern_count].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
            pattern_count++;
        }
    }

    if (pattern_count == 0) {
        return ESP_OK;
    }

    adc_continuous_config_t adc_config = {
        .pattern_num = pattern_count,
        .adc_pattern = adc_pattern,
        .sample_freq_hz = ADC_SAMPLE_FREQ_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };

    return adc_continuous_config(adc_handle, &adc_config);
}

esp_err_t set_active_adc_channels(uint16_t channel_mask)
{
    if (channel_mask == 0) {
        return ESP_OK;
    }

    esp_err_t err = ESP_OK;
    if (R_MUTEX_LOCK(adc_mutex, WAIT_FOREVER) != pdTRUE) {
        return ESP_FAIL;
    }

    if (is_adc_running) {
        err = adc_continuous_stop(adc_handle);
        if (err != ESP_OK) {
            goto exit;
        }
    }

    err = configure_adc_pattern(channel_mask);
    if (err != ESP_OK) {
        goto exit;
    }

    err = adc_continuous_start(adc_handle);
    if (err != ESP_OK) {
        goto exit;
    }

    is_adc_running = true;

exit:
    R_MUTEX_UNLOCK(adc_mutex);
    return err;
}

static void adc_processing_task_function(void *pvParameters)
{
    (void)pvParameters;

    uint8_t raw_buffer[ADC_FRAME_SIZE_BYTES];
    uint32_t ret_num = 0;

    while (1) {
        if (is_adc_running && R_MUTEX_LOCK(adc_mutex, MSEC(20)) == pdTRUE) {
            esp_err_t ret = adc_continuous_read(adc_handle, raw_buffer, ADC_FRAME_SIZE_BYTES, &ret_num, MSEC(10));

            if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
                uint32_t channel_sums[10] = {0};
                uint16_t channel_counts[10] = {0};

                for (int i = 0; i < (int)ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
                    adc_digi_output_data_t *p = (adc_digi_output_data_t*)&raw_buffer[i];
                    uint8_t chan = p->type2.channel;

                    if (chan < 10) {
                        channel_sums[chan] += p->type2.data;
                        channel_counts[chan]++;
                    }
                }

                for (int chan = 0; chan < 10; chan++) {
                    if (channel_counts[chan] > 0 && adc_pin_configs[chan] != NULL) {
                        
                        // IIR Filter processes constantly in background
                        uint32_t coarse_raw = channel_sums[chan] / channel_counts[chan];
                        internal_raw_filtered[chan] = (IIR_ALPHA * (float)coarse_raw) + ((1.0f - IIR_ALPHA) * internal_raw_filtered[chan]);

                        if (cali_handles[chan] != NULL) {
                            int voltage_mv = 0;
                            adc_cali_raw_to_voltage(cali_handles[chan], (int)internal_raw_filtered[chan], &voltage_mv);

                            esp_adc_pin_config_t* cfg = adc_pin_configs[chan];
                            
                            // Always update the true background reading
                            cfg->adc_last_read_mv = (uint16_t)voltage_mv;
                            
                            // Only update cache and evaluate alerts if NOT frozen
                            if (!is_frozen) {cfg->adc_cached_mv = (uint16_t)voltage_mv;}

                            if (cfg->callback != NULL) {
                                bool condition_met = false;
                                bool reset_condition_met = false;

                                // Determine if we are in the alert zone based on window_type
                                if (cfg->window_type == 0) { // SYS_GPIO_ADC_WINDOW_OUTSIDE
                                    condition_met = (voltage_mv >= cfg->adc_treshold_h_mv || voltage_mv <= cfg->adc_treshold_l_mv);
                                    reset_condition_met = (voltage_mv < (cfg->adc_treshold_h_mv - cfg->hysteresis_mv) &&
                                                            voltage_mv > (cfg->adc_treshold_l_mv + cfg->hysteresis_mv));
                                } 
                                else { // SYS_GPIO_ADC_WINDOW_INSIDE
                                    condition_met = (voltage_mv <= cfg->adc_treshold_h_mv && voltage_mv >= cfg->adc_treshold_l_mv);
                                    reset_condition_met = (voltage_mv > (cfg->adc_treshold_h_mv + cfg->hysteresis_mv) ||
                                                            voltage_mv < (cfg->adc_treshold_l_mv - cfg->hysteresis_mv));
                                }

                                // Trigger alert
                                if (condition_met && !cfg->alert_was_triggered) {
                                    cfg->alert_was_triggered = true;
                                    cfg->callback(cfg->callback_arg);
                                } 
                                // Reset alert state when safely past hysteresis
                                else if (reset_condition_met && cfg->alert_was_triggered) {
                                    cfg->alert_was_triggered = false;
                                }
                            }
                        }
                    }
                }
            }

            R_MUTEX_UNLOCK(adc_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t esp_adc_start(void)
{
    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = 4096,
        .conv_frame_size = ADC_FRAME_SIZE_BYTES,
    };

    esp_err_t err = adc_continuous_new_handle(&handle_cfg, &adc_handle);
    if (err != ESP_OK) {
        return err;
    }

    for (int i = 0; i < 10; i++) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .chan = i,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = SOC_ADC_DIGI_MAX_BITWIDTH,
        };

        err = adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handles[i]);
        if (err != ESP_OK) {
            return err;
        }
    }

    R_TASK_START_ON_CORE(adc_processing_task, adc_processing_task_function, NULL, 5, 1);
    return ESP_OK;
}

esp_err_t esp_adc_get_mv(uint8_t channel, uint16_t* out_mv)
{
    if (channel >= 10 || adc_pin_configs[channel] == NULL || out_mv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Safely grab the cached value
    if (R_MUTEX_LOCK(adc_mutex, WAIT_FOREVER) == pdTRUE) {
        *out_mv = adc_pin_configs[channel]->adc_cached_mv;
        R_MUTEX_UNLOCK(adc_mutex);
        return ESP_OK;
    }

    return ESP_FAIL;
}