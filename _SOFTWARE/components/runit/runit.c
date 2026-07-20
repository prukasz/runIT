#include "runit.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/projdefs.h"
#include "runit_board_cfg.h"
#include "runit_board_defs.h"
#include "sys_error_handler.h"
#include "sys_io.h"

static const char* TAG = "runit_test";

#define CHECK_AND_LOG(func_call)                                                                                                         \
  do {                                                                                                                                   \
    status_rep_t __status = (func_call);                                                                                                 \
    if (STA_IS_ERR(__status)) {                                                                                                          \
      ESP_LOGE(TAG, "%s Error %s, Owner %s", #func_call, status_error_to_name(__status.e_code), status_owner_to_name(__status.e_owner)); \
    }                                                                                                                                    \
  } while (0)

void runit_start(void) {
  sys_error_handler_init(SYS_BLE_CHT_RUNIT_STATUS, 1, SYS_BLE_CHR_RUNIT_LOGS, 1);
  CHECK_AND_LOG(sys_start_i2c());
  CHECK_AND_LOG(sys_io_static_config());
  CHECK_AND_LOG(sys_power_static_config());
  CHECK_AND_LOG(sys_ble_static_config());
  ESP_LOGI(TAG, "TRESTS");
  vTaskDelay(pdMS_TO_TICKS(1000));
  sys_io_set_pwm_frequency(DEVICE_ID_PCA9685, 0, 50);
  sys_io_set_pwm_duty(DEVICE_ID_PCA9685, 0, 200);
  vTaskDelay(pdMS_TO_TICKS(1000));
  sys_io_set_pwm_duty(DEVICE_ID_PCA9685, 0, 400);
}
