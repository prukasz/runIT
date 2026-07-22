#include "runit.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/projdefs.h"
#include "runit_board_cfg.h"
#include "runit_board_defs.h"
#include "sys_error_handler.h"
#include "sys_interface.h"
#include "sys_io.h"

static const char* TAG = "runit_app";

#define CHECK_AND_LOG(func_call)                                                                                                         \
  do {                                                                                                                                   \
    status_rep_t __status = (func_call);                                                                                                 \
    if (STA_IS_ERR(__status)) {                                                                                                          \
      ESP_LOGE(TAG, "%s Error %s, Owner %s", #func_call, status_error_to_name(__status.e_code), status_owner_to_name(__status.e_owner)); \
    }                                                                                                                                    \
  } while (0)

static void runit_ble_task(void* arg) {
  (void)arg;
  ESP_LOGI(TAG, "BLE RX processing task started");

  uint8_t rx_buf[512];
  size_t rx_len = 0;

  while (1) {
    if (STA_IS_OK(sys_ble_char_rx_dequeue(SYS_BLE_CHR_RUNIT_RX, rx_buf, sizeof(rx_buf) - 1, &rx_len)) && rx_len > 0) {
      rx_buf[rx_len] = '\0';
      ESP_LOGI(TAG, "Received BLE RX [%d bytes]: '%s'", (int)rx_len, (char*)rx_buf);
      
      sys_interface_decode(rx_buf, rx_len);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void runit_start(void) {
  CHECK_AND_LOG(sys_start_i2c());
  CHECK_AND_LOG(sys_io_static_config());
  CHECK_AND_LOG(sys_power_static_config());
  CHECK_AND_LOG(sys_ble_static_config());

  // Initialize error reporting and log redirection (esp_log_set_vprintf) after BLE services and buffer slots are active
  sys_error_handler_init(SYS_BLE_CHT_RUNIT_STATUS, 0, SYS_BLE_CHR_RUNIT_LOGS, 0);

  // Spawn BLE RX decoding worker task
  xTaskCreate(runit_ble_task, "runit_ble_task", 4096, NULL, 5, NULL);

  ESP_LOGI(TAG, "runIT system tasks and BLE interface initialized successfully");
}
