#include "driver_ap33772s.h"
#include <stdlib.h>
#include <string.h>
#include "rom/ets_sys.h"

// Identyfikator telemetryczny Menedżera I2C
#define I2C_FREQ_HZ 400000

#define RETURN_ON_ERROR(x)                   \
  do {                                       \
    esp_err_t __err_rc = (x);                \
    if (__err_rc != ESP_OK) return __err_rc; \
  } while (0)

#undef CHECK_HANDLE_R
#define CHECK_DRV_HANDLE(VAL)                 \
  do {                                      \
    if (!(VAL)) return ESP_ERR_INVALID_ARG; \
  } while (0)

/******************** Internal Platform Communications *************************/

static esp_err_t _ap33772s_read(ap33772s_handle_t handle, uint8_t reg, uint8_t* buf, size_t len) {
  CHECK_DRV_HANDLE(handle);
  CHECK_DRV_HANDLE(buf);
  // Użycie Twojego wrappera I2C
  return sys_i2c_master_transmit_receive(handle, &reg, 1, buf, len);
}

static esp_err_t _ap33772s_write(ap33772s_handle_t handle, uint8_t reg, const uint8_t* buf, size_t len) {
  CHECK_DRV_HANDLE(handle);
  CHECK_DRV_HANDLE(buf);

  if (len > 31) return ESP_ERR_INVALID_ARG;  // Sanity check for stack buffer size

  // Use stack buffer to prevent heap fragmentation in periodic background tasks
  uint8_t tx_data[32];
  tx_data[0] = reg;
  memcpy(&tx_data[1], buf, len);

  // Użycie Twojego wrappera I2C
  return sys_i2c_master_transmit(handle, tx_data, len + 1);
}

static int _current_map(int current_mA) {
  if (current_mA < 0 || current_mA > 5000) return -1;
  if (current_mA < 1250) return 0;
  return ((current_mA - 1250) / 250) + 1;
}

/******************** Background Service Worker *************************/

static void ap33772s_task(void* arg) {
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

      uint8_t payload[2] = {rdoData.byte0, rdoData.byte1};
      esp_err_t err = _ap33772s_write(handle, CMD_PD_REQMSG, payload, 2);
      if (err != ESP_OK && handle->error_callback) {
        handle->error_callback(handle->error_arg, err);
      }
    }
  }
}

/******************** TWO-PHASE INITIALIZATION *************************/

ap33772s_handle_t ap33772s_new(bool i2c_bus_num) {
  ap33772s_handle_t handle = calloc(1, sizeof(_ap33772s_data_t));
  if (!handle) {
    return NULL;
  }

  // Konfiguracja klasy bazowej
  handle->header.i2c_device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  handle->header.i2c_device_config.device_address = AP33772S_ADDRESS;
  handle->header.i2c_device_config.scl_speed_hz = I2C_FREQ_HZ;

  handle->header.bus_num = i2c_bus_num;

  handle->index_pps_user = -1;
  handle->index_avs_user = -1;
  handle->avs_active = false;
  handle->interrupt_triggered = false;

  return handle;
}

esp_err_t ap33772s_start(ap33772s_handle_t handle) {
  if (!handle) return ESP_ERR_INVALID_ARG;

  // 1. Rejestracja w I2C Manager
  /* The adapter registers the device with sys_i2c (and probes it) before
     calling start - registering here too added the same device twice. */

  // 2. Startowanie sprzężenia zwrotnego ISR i Keep-Alive AVS
  if (xTaskCreate(ap33772s_task, "ap33772s_svc", 3072, handle, 5, &handle->driver_task) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

void ap33772s_delete(ap33772s_handle_t handle) {
  if (handle) {
    if (handle->driver_task) {
      vTaskDelete(handle->driver_task);
    }
    free(handle);
  }
}

/******************** API Drivers Configurations *************************/

esp_err_t ap33772s_begin(ap33772s_handle_t handle) {
  CHECK_DRV_HANDLE(handle);

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
      handle->index_pps_user = i;
    } else if (i >= 8 && handle->src_pdo_array[i - 1].avs.type == 1) {
      handle->index_avs_user = i;
    }
  }
  return ESP_OK;
}

esp_err_t ap33772s_set_fixed_pdo(ap33772s_handle_t handle, int pdo_index, int max_current_mA) {
  CHECK_DRV_HANDLE(handle);
  if (max_current_mA <= 0 || pdo_index < 1 || pdo_index > 13) return ESP_ERR_INVALID_ARG;

  handle->avs_active = false;  // Disable any active keep-alives

  src_spr_and_epr_pdo_fields_t active_pdo = handle->src_pdo_array[pdo_index - 1];
  if (active_pdo.fixed.type != 0) {
    return ESP_ERR_INVALID_STATE;
  }

  // Safety check: Software limit to 22V
  bool isEPR = (pdo_index >= 8);  // index 1-7 is SPR, 8-13 is EPR
  int pdo_volt_mV = active_pdo.fixed.voltage_max * (isEPR ? 200 : 100);
  if (pdo_volt_mV > AP33772S_MAX_SOFTWARE_VOLTAGE_MV) {
    return ESP_ERR_INVALID_ARG;  // Abort
  }

  int mapped_curr = _current_map(max_current_mA);
  if (mapped_curr > active_pdo.fixed.current_max) {
    return ESP_ERR_INVALID_ARG;
  }

  rdo_data_t rdoData = {0};
  rdoData.REQMSG_Fields.PDO_INDEX = pdo_index;
  rdoData.REQMSG_Fields.CURRENT_SEL = mapped_curr;

  uint8_t payload[2] = {rdoData.byte0, rdoData.byte1};
  return _ap33772s_write(handle, CMD_PD_REQMSG, payload, 2);
}

esp_err_t ap33772s_set_pps_pdo(ap33772s_handle_t handle, int pdo_index, int target_voltage_mV, int max_current_mA) {
  CHECK_DRV_HANDLE(handle);
  if (pdo_index < 1 || pdo_index > 7) return ESP_ERR_INVALID_ARG;

  handle->avs_active = false;  // Disable any active keep-alives

  src_spr_and_epr_pdo_fields_t active_pdo = handle->src_pdo_array[pdo_index - 1];
  if (active_pdo.pps.type != 1) return ESP_ERR_INVALID_STATE;

  // Safety Check: Clamp Software Target to 22V Max
  if (target_voltage_mV > AP33772S_MAX_SOFTWARE_VOLTAGE_MV) {
    target_voltage_mV = AP33772S_MAX_SOFTWARE_VOLTAGE_MV;
  }

  int mapped_curr = _current_map(max_current_mA);
  if (mapped_curr > active_pdo.pps.current_max) return ESP_ERR_INVALID_ARG;

  int voltage_min_decoded = (active_pdo.pps.voltage_min > 0) ? 3300 : 0;
  if (target_voltage_mV < voltage_min_decoded || target_voltage_mV > (active_pdo.pps.voltage_max * 100)) {
    return ESP_ERR_INVALID_ARG;
  }

  rdo_data_t rdoData = {0};
  rdoData.REQMSG_Fields.PDO_INDEX = pdo_index;
  rdoData.REQMSG_Fields.VOLTAGE_SEL = target_voltage_mV / 100;
  rdoData.REQMSG_Fields.CURRENT_SEL = mapped_curr;

  uint8_t payload[2] = {rdoData.byte0, rdoData.byte1};
  return _ap33772s_write(handle, CMD_PD_REQMSG, payload, 2);
}

esp_err_t ap33772s_set_avs_pdo(ap33772s_handle_t handle, int pdo_index, int target_voltage_mV, int max_current_mA) {
  CHECK_DRV_HANDLE(handle);
  if (pdo_index < 8 || pdo_index > 13) return ESP_ERR_INVALID_ARG;

  src_spr_and_epr_pdo_fields_t active_pdo = handle->src_pdo_array[pdo_index - 1];
  if (active_pdo.avs.type != 1) return ESP_ERR_INVALID_STATE;

  // Safety Check: Clamp Software Target to 22V Max
  if (target_voltage_mV > AP33772S_MAX_SOFTWARE_VOLTAGE_MV) {
    target_voltage_mV = AP33772S_MAX_SOFTWARE_VOLTAGE_MV;
  }

  int mapped_curr = _current_map(max_current_mA);
  if (mapped_curr > active_pdo.avs.current_max) return ESP_ERR_INVALID_ARG;

  int voltage_min_decoded = (active_pdo.avs.voltage_min > 0) ? 15000 : 0;
  if (target_voltage_mV < voltage_min_decoded || target_voltage_mV > (active_pdo.avs.voltage_max * 200)) {
    return ESP_ERR_INVALID_ARG;
  }

  rdo_data_t rdoData = {0};
  rdoData.REQMSG_Fields.PDO_INDEX = pdo_index;
  rdoData.REQMSG_Fields.VOLTAGE_SEL = target_voltage_mV / 200;
  rdoData.REQMSG_Fields.CURRENT_SEL = mapped_curr;

  uint8_t payload[2] = {rdoData.byte0, rdoData.byte1};
  RETURN_ON_ERROR(_ap33772s_write(handle, CMD_PD_REQMSG, payload, 2));

  // Stash parameters and flag loop to begin sending keep-alives every 500ms
  handle->index_avs_cache = rdoData.REQMSG_Fields.PDO_INDEX;
  handle->voltage_avs_byte_cache = rdoData.REQMSG_Fields.VOLTAGE_SEL;
  handle->current_avs_byte_cache = rdoData.REQMSG_Fields.CURRENT_SEL;
  handle->avs_active = true;

  return ESP_OK;
}

esp_err_t ap33772s_set_output(ap33772s_handle_t handle, bool enable) {
  uint8_t flag = enable ? 0x12 : 0x11;  // 0b00010010 (ON) or 0b00010011 (OFF)
  return _ap33772s_write(handle, CMD_SYSTEM, &flag, 1);
}

/******************** Telemetry Read Operations *************************/

int ap33772s_read_temp(ap33772s_handle_t handle) {
  uint8_t val = 0;
  if (_ap33772s_read(handle, CMD_TEMP, &val, 1) != ESP_OK) return -1;
  return val;
}

int ap33772s_read_voltage(ap33772s_handle_t handle) {
  uint8_t buf[2] = {0};
  if (_ap33772s_read(handle, CMD_VOLTAGE, buf, 2) != ESP_OK) return -1;
  return ((buf[1] << 8) | buf[0]) * 80;  // 80mV/LSB
}

int ap33772s_read_current(ap33772s_handle_t handle) {
  uint8_t val = 0;
  if (_ap33772s_read(handle, CMD_CURRENT, &val, 1) != ESP_OK) return -1;
  return val * 24;  // 24mA/LSB
}

int ap33772s_read_vreq(ap33772s_handle_t handle) {
  uint8_t val = 0;
  if (_ap33772s_read(handle, CMD_VREQ, &val, 1) != ESP_OK) return -1;
  return val * 50;  // 50mV/LSB
}

int ap33772s_read_ireq(ap33772s_handle_t handle) {
  uint8_t val = 0;
  if (_ap33772s_read(handle, CMD_IREQ, &val, 1) != ESP_OK) return -1;
  return val * 10;  // 10mA/LSB
}

/******************** Peripheral Customizers *************************/

esp_err_t ap33772s_set_ntc(ap33772s_handle_t handle, int tr25, int tr50, int tr75, int tr100) {
  uint8_t payload[2];
  bool scheduler_running = (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED);

  payload[0] = tr25 & 0xFF;
  payload[1] = (tr25 >> 8) & 0xFF;
  RETURN_ON_ERROR(_ap33772s_write(handle, CMD_TR25, payload, 2));
  if (scheduler_running)
    vTaskDelay(pdMS_TO_TICKS(5));
  else
    ets_delay_us(5000);

  payload[0] = tr50 & 0xFF;
  payload[1] = (tr50 >> 8) & 0xFF;
  RETURN_ON_ERROR(_ap33772s_write(handle, CMD_TR50, payload, 2));
  if (scheduler_running)
    vTaskDelay(pdMS_TO_TICKS(5));
  else
    ets_delay_us(5000);

  payload[0] = tr75 & 0xFF;
  payload[1] = (tr75 >> 8) & 0xFF;
  RETURN_ON_ERROR(_ap33772s_write(handle, CMD_TR75, payload, 2));
  if (scheduler_running)
    vTaskDelay(pdMS_TO_TICKS(5));
  else
    ets_delay_us(5000);

  payload[0] = tr100 & 0xFF;
  payload[1] = (tr100 >> 8) & 0xFF;
  RETURN_ON_ERROR(_ap33772s_write(handle, CMD_TR100, payload, 2));

  return ESP_OK;
}

int ap33772s_read_vselmin(ap33772s_handle_t handle) {
  uint8_t val = 0;
  if (_ap33772s_read(handle, CMD_VSELMIN, &val, 1) != ESP_OK) return -1;
  return val * 200;
}

esp_err_t ap33772s_set_vselmin(ap33772s_handle_t handle, int voltage_mV) {
  uint8_t val = voltage_mV / 200;
  return _ap33772s_write(handle, CMD_VSELMIN, &val, 1);
}

int ap33772s_read_uvp_threshold(ap33772s_handle_t handle) {
  uint8_t val = 0;
  if (_ap33772s_read(handle, CMD_UVPTHR, &val, 1) != ESP_OK) return -1;
  if (val == 1) return 80;
  if (val == 2) return 75;
  if (val == 3) return 70;
  return -1;
}

esp_err_t ap33772s_set_uvp_threshold(ap33772s_handle_t handle, int percentage) {
  uint8_t val;
  if (percentage == 80)
    val = 1;
  else if (percentage == 75)
    val = 2;
  else if (percentage == 70)
    val = 3;
  else
    return ESP_ERR_INVALID_ARG;
  return _ap33772s_write(handle, CMD_UVPTHR, &val, 1);
}

int ap33772s_read_ovp_threshold(ap33772s_handle_t handle) {
  uint8_t val = 0;
  if (_ap33772s_read(handle, CMD_OVPTHR, &val, 1) != ESP_OK) return -1;
  return val * 80;
}

esp_err_t ap33772s_set_ovp_threshold(ap33772s_handle_t handle, int voltage_mV) {
  uint8_t val = voltage_mV / 80;
  return _ap33772s_write(handle, CMD_OVPTHR, &val, 1);
}

/******************** Interrupt Controls *************************/

uint8_t ap33772s_read_status(ap33772s_handle_t handle) {
  uint8_t val = 0;
  if (_ap33772s_read(handle, CMD_STATUS, &val, 1) != ESP_OK) return 0;
  return val;
}

void ap33772s_register_interrupt(ap33772s_handle_t handle, void (*callback)(void*), void* arg) {
  if (handle) {
    handle->user_isr_callback = callback;
    handle->user_isr_arg = arg;
  }
}

IRAM_ATTR void ap33772s_intr_handler(void* arg) {
  ap33772s_handle_t handle = (ap33772s_handle_t)arg;
  if (!handle) return;

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  handle->interrupt_triggered = true;

  // Zmiana na referencję do dziedziczonego taska
  if (handle->driver_task) {
    xTaskNotifyFromISR(handle->driver_task, 1, eSetBits, &xHigherPriorityTaskWoken);
  }

  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
