#include "ap33772s.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "rom/ets_sys.h" 

static const char* TAG = "AP33772S";
#define AP33772S_I2C_TIMEOUT_MS 40
#define I2C_FREQ_HZ             400000

#define RETURN_ON_ERROR(x) do {               \
    esp_err_t __err_rc = (x);                 \
    if (__err_rc != ESP_OK) return __err_rc;  \
} while (0)

#define CHECK_HANDLE_R(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)

/******************** Internal Platform Communications *************************/

static esp_err_t _ap33772s_read(ap33772s_handle_t handle, uint8_t reg, uint8_t *buf, size_t len)
{
    CHECK_HANDLE_R(handle);
    CHECK_HANDLE_R(buf);
    return i2c_master_transmit_receive(handle->i2c_master_dev_handle, &reg, 1, buf, len, AP33772S_I2C_TIMEOUT_MS);
}

static esp_err_t _ap33772s_write(ap33772s_handle_t handle, uint8_t reg, const uint8_t *buf, size_t len)
{
    CHECK_HANDLE_R(handle);
    CHECK_HANDLE_R(buf);

    if (len > 31) return ESP_ERR_INVALID_ARG; // Sanity check for stack buffer size

    // Use stack buffer to prevent heap fragmentation in periodic background tasks
    uint8_t tx_data[32];
    tx_data[0] = reg;
    memcpy(&tx_data[1], buf, len);

    return i2c_master_transmit(handle->i2c_master_dev_handle, tx_data, len + 1, AP33772S_I2C_TIMEOUT_MS);
}

static int _current_map(int current_ma)
{
    if (current_ma < 0 || current_ma > 5000) return -1;
    if (current_ma < 1250) return 0;
    return ((current_ma - 1250) / 250) + 1;
}

static void _log_current_range(unsigned int current_max)
{
    switch (current_max) {
        case 0:  printf("0.00A ~ 1.24A\n"); break;
        case 1:  printf("1.25A ~ 1.49A\n"); break;
        case 2:  printf("1.50A ~ 1.74A\n"); break;
        case 3:  printf("1.75A ~ 1.99A\n"); break;
        case 4:  printf("2.00A ~ 2.24A\n"); break;
        case 5:  printf("2.25A ~ 2.49A\n"); break;
        case 6:  printf("2.50A ~ 2.74A\n"); break;
        case 7:  printf("2.75A ~ 2.99A\n"); break;
        case 8:  printf("3.00A ~ 3.24A\n"); break;
        case 9:  printf("3.25A ~ 3.49A\n"); break;
        case 10: printf("3.50A ~ 3.74A\n"); break;
        case 11: printf("3.75A ~ 3.99A\n"); break;
        case 12: printf("4.00A ~ 4.24A\n"); break;
        case 13: printf("4.25A ~ 4.49A\n"); break;
        case 14: printf("4.50A ~ 4.99A\n"); break;
        case 15: printf("5.00A +\n"); break;
        default: printf("Invalid\n"); break;
    }
}

/******************** Background Service Worker *************************/

static void ap33772s_task(void *arg)
{
    ap33772s_handle_t handle = (ap33772s_handle_t)arg;
    
    while (1) {
        uint32_t notification_value = 0;
        
        // Block until interrupted by ISR, or loop naturally every 500ms
        BaseType_t notified = xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, pdMS_TO_TICKS(500));

        // 1. Process Hardware Interrupt Deferred Call
        if (notified == pdTRUE) {
            if (handle->interrupt_triggered) {
                handle->interrupt_triggered = false;
                if (handle->user_isr_callback) {
                    handle->user_isr_callback(handle->user_isr_arg);
                }
            }
        } 
        
        // 2. Process Periodic AVS Keep-Alive (Fires constantly if AVS is negotiated)
        if (handle->avs_active) {
            rdo_data_t rdoData = {0};
            rdoData.REQMSG_Fields.PDO_INDEX = handle->index_avs_cache;
            rdoData.REQMSG_Fields.VOLTAGE_SEL = handle->voltage_avs_byte_cache;
            rdoData.REQMSG_Fields.CURRENT_SEL = handle->current_avs_byte_cache;

            uint8_t payload[2] = { rdoData.byte0, rdoData.byte1 };
            if (_ap33772s_write(handle, CMD_PD_REQMSG, payload, 2) != ESP_OK) {
                ESP_LOGE(TAG, "Failed sending AVS keep-alive token update.");
            }
        }
    }
}

/******************** API Drivers Configurations *************************/

ap33772s_handle_t ap33772s_new(void)
{
    ap33772s_handle_t handle = calloc(1, sizeof(_ap33772s_data_t));
    if (!handle) {
        ESP_LOGE(TAG, "Heap allocation failed context creation.");
        return NULL;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AP33772S_ADDRESS,
        .scl_speed_hz = I2C_FREQ_HZ
    };

    handle->i2c_device_config = dev_cfg;
    handle->index_pps_user = -1;
    handle->index_avs_user = -1;
    handle->avs_active = false;
    handle->interrupt_triggered = false;

    // Start hybrid periodic/deferred ISR task
    if (xTaskCreate(ap33772s_task, "ap33772s_svc", 3072, handle, 5, &handle->driver_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Unable to start background resource loop.");
        free(handle);
        return NULL;
    }

    return handle;
}

void ap33772s_delete(ap33772s_handle_t handle)
{
    if (handle) {
        if (handle->driver_task_handle) {
            vTaskDelete(handle->driver_task_handle);
        }
        free(handle);
    }
}

esp_err_t ap33772s_begin(ap33772s_handle_t handle)
{
    CHECK_HANDLE_R(handle);
    
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        vTaskDelay(pdMS_TO_TICKS(100));
    } else {
        ets_delay_us(100000);
    }

    uint8_t raw_pdo_data[26] = {0};
    RETURN_ON_ERROR(_ap33772s_read(handle, CMD_SRCPDO, raw_pdo_data, 26));

    for (int i = 0; i < 26; i += 2) {
        int pdoIndex = (i / 2);
        handle->src_pdo_array[pdoIndex].byte0 = raw_pdo_data[i];
        handle->src_pdo_array[pdoIndex].byte1 = raw_pdo_data[i + 1];
    }

    // Map profiles
    for (int i = 1; i <= 13; i++) {
        if (i < 8 && handle->src_pdo_array[i - 1].pps.type == 1) {
            ESP_LOGI(TAG, "Discovered active PPS capability profile slot: %d", i);
            handle->index_pps_user = i;
        } else if (i >= 8 && handle->src_pdo_array[i - 1].avs.type == 1) {
            ESP_LOGI(TAG, "Discovered active AVS capability profile slot: %d", i);
            handle->index_avs_user = i;
        }
    }
    return ESP_OK;
}

void ap33772s_log_profiles(ap33772s_handle_t handle)
{
    if (!handle) return;
    printf("--- AP33772S Target Profiles List ---\n");
    for (int i = 0; i < MAX_PDO_ENTRIES; i++) {
        bool isEPR = (i >= 7 && i <= 12);
        if (handle->src_pdo_array[i].byte0 == 0 && handle->src_pdo_array[i].byte1 == 0) continue;

        printf("Slot %d [%s]: ", i + 1, isEPR ? "EPR" : "SPR");
        if (handle->src_pdo_array[i].fixed.type == 0) {
            printf("Fixed Output -> %dmV, Limit: ", handle->src_pdo_array[i].fixed.voltage_max * (isEPR ? 200 : 100));
            _log_current_range(handle->src_pdo_array[i].fixed.current_max);
        } else {
            printf("%s Output -> Max %dmV, Limit: ", isEPR ? "AVS" : "PPS", handle->src_pdo_array[i].fixed.voltage_max * (isEPR ? 200 : 100));
            _log_current_range(handle->src_pdo_array[i].fixed.current_max);
        }
    }
}

esp_err_t ap33772s_set_fixed_pdo(ap33772s_handle_t handle, int pdo_index, int max_current_ma)
{
    CHECK_HANDLE_R(handle);
    if (max_current_ma <= 0 || pdo_index < 1 || pdo_index > 13) return ESP_ERR_INVALID_ARG;

    handle->avs_active = false; // Disable any active keep-alives
    
    src_spr_and_epr_pdo_fields_t active_pdo = handle->src_pdo_array[pdo_index - 1];
    if (active_pdo.fixed.type != 0) {
        ESP_LOGE(TAG, "Targeted index profiles are not evaluated as Fixed Rails configurations.");
        return ESP_ERR_INVALID_STATE;
    }

    // Safety check: Software limit to 22V
    bool isEPR = (pdo_index >= 8); // index 1-7 is SPR, 8-13 is EPR
    int pdo_volt_mv = active_pdo.fixed.voltage_max * (isEPR ? 200 : 100);
    if (pdo_volt_mv > AP33772S_MAX_SOFTWARE_VOLTAGE_MV) {
        ESP_LOGE(TAG, "Software lock: Fixed PDO %d voltage (%dmV) exceeds %dmV limit.", pdo_index, pdo_volt_mv, AP33772S_MAX_SOFTWARE_VOLTAGE_MV);
        return ESP_ERR_INVALID_ARG; // Abort
    }

    int mapped_curr = _current_map(max_current_ma);
    if (mapped_curr > active_pdo.fixed.current_max) {
        ESP_LOGE(TAG, "Requested fixed limits overshoot current safety specs bounds.");
        return ESP_ERR_INVALID_ARG;
    }

    rdo_data_t rdoData = {0};
    rdoData.REQMSG_Fields.PDO_INDEX = pdo_index;
    rdoData.REQMSG_Fields.CURRENT_SEL = mapped_curr;

    uint8_t payload[2] = { rdoData.byte0, rdoData.byte1 };
    return _ap33772s_write(handle, CMD_PD_REQMSG, payload, 2);
}

esp_err_t ap33772s_set_pps_pdo(ap33772s_handle_t handle, int pdo_index, int target_voltage_mv, int max_current_ma)
{
    CHECK_HANDLE_R(handle);
    if (pdo_index < 1 || pdo_index > 7) return ESP_ERR_INVALID_ARG;

    handle->avs_active = false; // Disable any active keep-alives

    src_spr_and_epr_pdo_fields_t active_pdo = handle->src_pdo_array[pdo_index - 1];
    if (active_pdo.pps.type != 1) return ESP_ERR_INVALID_STATE;

    // Safety Check: Clamp Software Target to 22V Max
    if (target_voltage_mv > AP33772S_MAX_SOFTWARE_VOLTAGE_MV) {
        ESP_LOGW(TAG, "Software lock: Clamping requested PPS voltage from %dmV to %dmV", target_voltage_mv, AP33772S_MAX_SOFTWARE_VOLTAGE_MV);
        target_voltage_mv = AP33772S_MAX_SOFTWARE_VOLTAGE_MV;
    }

    int mapped_curr = _current_map(max_current_ma);
    if (mapped_curr > active_pdo.pps.current_max) return ESP_ERR_INVALID_ARG;

    int voltage_min_decoded = (active_pdo.pps.voltage_min > 0) ? 3300 : 0;
    if (target_voltage_mv < voltage_min_decoded || target_voltage_mv > (active_pdo.pps.voltage_max * 100)) {
        ESP_LOGE(TAG, "PPS target tracking value out of range bounds.");
        return ESP_ERR_INVALID_ARG;
    }

    rdo_data_t rdoData = {0};
    rdoData.REQMSG_Fields.PDO_INDEX = pdo_index;
    rdoData.REQMSG_Fields.VOLTAGE_SEL = target_voltage_mv / 100;
    rdoData.REQMSG_Fields.CURRENT_SEL = mapped_curr;

    uint8_t payload[2] = { rdoData.byte0, rdoData.byte1 };
    return _ap33772s_write(handle, CMD_PD_REQMSG, payload, 2);
}

esp_err_t ap33772s_set_avs_pdo(ap33772s_handle_t handle, int pdo_index, int target_voltage_mv, int max_current_ma)
{
    CHECK_HANDLE_R(handle);
    if (pdo_index < 8 || pdo_index > 13) return ESP_ERR_INVALID_ARG;

    src_spr_and_epr_pdo_fields_t active_pdo = handle->src_pdo_array[pdo_index - 1];
    if (active_pdo.avs.type != 1) return ESP_ERR_INVALID_STATE;

    // Safety Check: Clamp Software Target to 22V Max
    if (target_voltage_mv > AP33772S_MAX_SOFTWARE_VOLTAGE_MV) {
        ESP_LOGW(TAG, "Software lock: Clamping requested AVS voltage from %dmV to %dmV", target_voltage_mv, AP33772S_MAX_SOFTWARE_VOLTAGE_MV);
        target_voltage_mv = AP33772S_MAX_SOFTWARE_VOLTAGE_MV;
    }

    int mapped_curr = _current_map(max_current_ma);
    if (mapped_curr > active_pdo.avs.current_max) return ESP_ERR_INVALID_ARG;

    int voltage_min_decoded = (active_pdo.avs.voltage_min > 0) ? 15000 : 0;
    if (target_voltage_mv < voltage_min_decoded || target_voltage_mv > (active_pdo.avs.voltage_max * 200)) {
        ESP_LOGE(TAG, "AVS target value context boundary violation detected.");
        return ESP_ERR_INVALID_ARG;
    }

    rdo_data_t rdoData = {0};
    rdoData.REQMSG_Fields.PDO_INDEX = pdo_index;
    rdoData.REQMSG_Fields.VOLTAGE_SEL = target_voltage_mv / 200;
    rdoData.REQMSG_Fields.CURRENT_SEL = mapped_curr;

    uint8_t payload[2] = { rdoData.byte0, rdoData.byte1 };
    RETURN_ON_ERROR(_ap33772s_write(handle, CMD_PD_REQMSG, payload, 2));

    // Stash parameters and flag loop to begin sending keep-alives every 500ms
    handle->index_avs_cache = rdoData.REQMSG_Fields.PDO_INDEX;
    handle->voltage_avs_byte_cache = rdoData.REQMSG_Fields.VOLTAGE_SEL;
    handle->current_avs_byte_cache = rdoData.REQMSG_Fields.CURRENT_SEL;
    handle->avs_active = true;

    return ESP_OK;
}

esp_err_t ap33772s_set_output(ap33772s_handle_t handle, bool enable)
{
    uint8_t flag = enable ? 0x12 : 0x11; // 0b00010010 (ON) or 0b00010011 (OFF)
    return _ap33772s_write(handle, CMD_SYSTEM, &flag, 1);
}

/******************** Telemetry Read Operations *************************/

int ap33772s_read_temp(ap33772s_handle_t handle)
{
    uint8_t val = 0;
    if (_ap33772s_read(handle, CMD_TEMP, &val, 1) != ESP_OK) return -1;
    return val;
}

int ap33772s_read_voltage(ap33772s_handle_t handle)
{
    uint8_t buf[2] = {0};
    if (_ap33772s_read(handle, CMD_VOLTAGE, buf, 2) != ESP_OK) return -1;
    return ((buf[1] << 8) | buf[0]) * 80; // 80mV/LSB
}

int ap33772s_read_current(ap33772s_handle_t handle)
{
    uint8_t val = 0;
    if (_ap33772s_read(handle, CMD_CURRENT, &val, 1) != ESP_OK) return -1;
    return val * 24; // 24mA/LSB
}

int ap33772s_read_vreq(ap33772s_handle_t handle)
{
    uint8_t val = 0;
    if (_ap33772s_read(handle, CMD_VREQ, &val, 1) != ESP_OK) return -1;
    return val * 50; // 50mV/LSB
}

int ap33772s_read_ireq(ap33772s_handle_t handle)
{
    uint8_t val = 0;
    if (_ap33772s_read(handle, CMD_IREQ, &val, 1) != ESP_OK) return -1;
    return val * 10; // 10mA/LSB
}

/******************** Peripheral Customizers *************************/

esp_err_t ap33772s_set_ntc(ap33772s_handle_t handle, int tr25, int tr50, int tr75, int tr100)
{
    uint8_t payload[2];
    bool scheduler_running = (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED);
    
    payload[0] = tr25 & 0xFF; payload[1] = (tr25 >> 8) & 0xFF;
    RETURN_ON_ERROR(_ap33772s_write(handle, CMD_TR25, payload, 2));
    if (scheduler_running) vTaskDelay(pdMS_TO_TICKS(5)); else ets_delay_us(5000);

    payload[0] = tr50 & 0xFF; payload[1] = (tr50 >> 8) & 0xFF;
    RETURN_ON_ERROR(_ap33772s_write(handle, CMD_TR50, payload, 2));
    if (scheduler_running) vTaskDelay(pdMS_TO_TICKS(5)); else ets_delay_us(5000);

    payload[0] = tr75 & 0xFF; payload[1] = (tr75 >> 8) & 0xFF;
    RETURN_ON_ERROR(_ap33772s_write(handle, CMD_TR75, payload, 2));
    if (scheduler_running) vTaskDelay(pdMS_TO_TICKS(5)); else ets_delay_us(5000);

    payload[0] = tr100 & 0xFF; payload[1] = (tr100 >> 8) & 0xFF;
    RETURN_ON_ERROR(_ap33772s_write(handle, CMD_TR100, payload, 2));
    
    return ESP_OK;
}

int ap33772s_read_vselmin(ap33772s_handle_t handle)
{
    uint8_t val = 0;
    if (_ap33772s_read(handle, CMD_VSELMIN, &val, 1) != ESP_OK) return -1;
    return val * 200;
}

esp_err_t ap33772s_set_vselmin(ap33772s_handle_t handle, int voltage_mv)
{
    uint8_t val = voltage_mv / 200;
    return _ap33772s_write(handle, CMD_VSELMIN, &val, 1);
}

int ap33772s_read_uvp_threshold(ap33772s_handle_t handle)
{
    uint8_t val = 0;
    if (_ap33772s_read(handle, CMD_UVPTHR, &val, 1) != ESP_OK) return -1;
    if (val == 1) return 80;
    if (val == 2) return 75;
    if (val == 3) return 70;
    return -1;
}

esp_err_t ap33772s_set_uvp_threshold(ap33772s_handle_t handle, int percentage)
{
    uint8_t val;
    if (percentage == 80) val = 1;
    else if (percentage == 75) val = 2;
    else if (percentage == 70) val = 3;
    else return ESP_ERR_INVALID_ARG;
    return _ap33772s_write(handle, CMD_UVPTHR, &val, 1);
}

int ap33772s_read_ovp_threshold(ap33772s_handle_t handle)
{
    uint8_t val = 0;
    if (_ap33772s_read(handle, CMD_OVPTHR, &val, 1) != ESP_OK) return -1;
    return val * 80;
}

esp_err_t ap33772s_set_ovp_threshold(ap33772s_handle_t handle, int voltage_mv)
{
    uint8_t val = voltage_mv / 80;
    return _ap33772s_write(handle, CMD_OVPTHR, &val, 1);
}

/******************** Interrupt Controls *************************/

uint8_t ap33772s_read_status(ap33772s_handle_t handle)
{
    uint8_t val = 0;
    if (_ap33772s_read(handle, CMD_STATUS, &val, 1) != ESP_OK) return 0;
    return val;
}

void ap33772s_register_interrupt(ap33772s_handle_t handle, void (*callback)(void *), void *arg)
{
    if (handle) {
        handle->user_isr_callback = callback;
        handle->user_isr_arg = arg;
    }
}

IRAM_ATTR void ap33772s_intr_handler(void *arg)
{
    ap33772s_handle_t handle = (ap33772s_handle_t)arg;
    if (!handle) return;
    
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    handle->interrupt_triggered = true; 
    
    if (handle->driver_task_handle) {
        xTaskNotifyFromISR(handle->driver_task_handle, 1, eSetBits, &xHigherPriorityTaskWoken);
    }
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}