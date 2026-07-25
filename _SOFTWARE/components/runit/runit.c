#include "runit.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/projdefs.h"
#include "runit_board_cfg.h"
#include "runit_board_defs.h"
#include "sys_interface.h"
static const char* TAG = "runit_app";

#define CHECK_AND_LOG(func_call)                                                                                 \
  do {                                                                                                           \
    err_h __status = (func_call);                                                                                \
    if (SE_IS_ERR(__status)) {                                                                                   \
      ESP_LOGE(TAG, "%s Error Tag %d, Owner %u", #func_call, (int)__status->tag, (unsigned int)__status->owner); \
    }                                                                                                            \
  } while (0)

static void runit_ble_task(void* arg) {
  (void)arg;
  ESP_LOGI(TAG, "BLE RX processing task started");

  uint8_t rx_buf[512];
  size_t rx_len = 0;

  while (1) {
    if (sys_ble_char_rx_dequeue(SYS_BLE_CHR_RUNIT_RX, rx_buf, sizeof(rx_buf) - 1, &rx_len) == NULL && rx_len > 0) {
      rx_buf[rx_len] = '\0';
      ESP_LOGI(TAG, "Received BLE RX [%d bytes]: '%s'", (int)rx_len, (char*)rx_buf);

      sys_interface_decode(rx_buf, rx_len);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

static void runit_adc_test_task(void* arg) {
  (void)arg;
  ESP_LOGI(TAG, "ADC test task started for GPIO pins 5, 6, and 7");
  uint32_t mv5 = 0, mv6 = 0, mv7 = 0;

  while (1) {
    err_h err5 = sys_io_get_voltage(DEVICE_ID_GPIO_ESP, 5, &mv5);
    err_h err6 = sys_io_get_voltage(DEVICE_ID_GPIO_ESP, 6, &mv6);
    err_h err7 = sys_io_get_voltage(DEVICE_ID_GPIO_ESP, 7, &mv7);

    if (SE_IS_OK(err5) && SE_IS_OK(err6) && SE_IS_OK(err7)) {
      ESP_LOGI(TAG, "ADC Readings [mV] -> GPIO5: %lu mV | GPIO6: %lu mV | GPIO7: %lu mV",
               (unsigned long)mv5, (unsigned long)mv6, (unsigned long)mv7);
    } else {
      ESP_LOGW(TAG, "ADC read warning on GPIO 5-7");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void runit_start(void) {
  SE_init();
  CHECK_AND_LOG(sys_start_i2c());
  CHECK_AND_LOG(sys_io_static_config());
  CHECK_AND_LOG(sys_power_static_config());
  CHECK_AND_LOG(sys_ble_static_config());

  // Configure ESP GPIO pins 5, 6, 7 as ADC mode
  CHECK_AND_LOG(sys_io_set_mode(DEVICE_ID_GPIO_ESP, 5, SYS_IO_MODE_ADC));
  CHECK_AND_LOG(sys_io_set_mode(DEVICE_ID_GPIO_ESP, 6, SYS_IO_MODE_ADC));
  CHECK_AND_LOG(sys_io_set_mode(DEVICE_ID_GPIO_ESP, 7, SYS_IO_MODE_ADC));

  // Spawn BLE RX decoding worker task
  xTaskCreate(runit_ble_task, "runit_ble_task", 4096, NULL, 5, NULL);

  // Spawn ADC test task
  xTaskCreate(runit_adc_test_task, "adc_test_task", 4096, NULL, 4, NULL);

  ESP_LOGI(TAG, "runIT system tasks, BLE interface, and ADC test task initialized successfully");
}
