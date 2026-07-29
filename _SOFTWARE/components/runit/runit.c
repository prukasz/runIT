#include "runit.h"
#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "dec_sys_contracts.h"
#include "freertos/projdefs.h"
#include "runit_board_cfg.h"
#include "runit_board_defs.h"
#include "sys_actions.h"
#include "sys_interface.h"
// dec_sys_contracts.h leaves OWNER set to OWNER_DEC_SYS_CONTRACTS; runit.c doesn't
// emit its own SE_* errors, but undef it so that stays explicit rather than implicit.
#undef OWNER
static const char* TAG = "runit_app";

#define CHECK_AND_LOG(func_call)                                                                                 \
  do {                                                                                                           \
    err_h __status = (func_call);                                                                                \
    if (SE_IS_ERR(__status)) {                                                                                   \
      ESP_LOGE(TAG, "%s Error Tag %d, Owner %u", #func_call, (int)__status->tag, (unsigned int)__status->owner); \
    }                                                                                                            \
  } while (0)

/**
 * @brief Board-level logging / error-telemetry policy.
 *
 * Logs are mirrored on serial and streamed as whole lines to the LOGS
 * characteristic; error chains go out the same characteristic under their own
 * TX header, encoded by enc_sys_errors_encode_chain() for client-side
 * reconstruction. Filtering is global_level only - use esp_log_level_set() for
 * per-tag overrides. See SYS_ERRORS.MD for the full field reference.
 */
static const sys_error_cfg_t s_runit_error_cfg = {
    .global_level = ESP_LOG_INFO,
    .logs =
        {
            .mirror_on_serial = true,
            .ble_enable = true,
            .char_uuid = SYS_BLE_CHR_RUNIT_LOGS,
            .tx_header = PACKET_HEADER_LOGS,
        },
    .errors =
        {
            .serial_trace = true,
            .ble_enable = true,
            .char_uuid = SYS_BLE_CHR_RUNIT_LOGS,
            .tx_header = PACKET_HEADER_ERRORS,
            .packet_max = SE_ERR_PACKET_MAX,
        },
};

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

/**
 * @brief One-shot injection test: pushes crafted class-0x01 frames straight into
 * the RUNIT_RX ring buffer via sys_ble_char_rx_inject(), exercising the full
 * pipeline (buffer -> semaphore -> sys_if_rx pump -> sys_interface_decode ->
 * dec_sys_contracts) without a connected BLE peer. Exercises PCA9685 (PWM
 * output) and gpio_esp (ADC input) - watch the sys_if_rx/dec_sys_contracts
 * ESP_LOGI lines for the result of each injected frame.
 */
static void runit_injection_test_task(void* arg) {
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(500));  // let sys_interface_bind_ble_rx()'s pump task start first
  ESP_LOGI(TAG, "Injection test: pushing crafted frames into RUNIT_RX buffer");

  // PCA9685 (device 3, channel 0): set PWM duty to half scale. class 0x01 / packet 0x28.
  packet_sys_io_set_pwm_duty_t pca_duty = {.device_id = DEVICE_ID_PCA9685, .pin = 0, .duty = 2048};
  uint8_t frame_pca[2 + sizeof(pca_duty)] = {SYS_CONTRACTS_CLASS_HEADER, HEADER_packet_sys_io_set_pwm_duty_t};
  memcpy(&frame_pca[2], &pca_duty, sizeof(pca_duty));
  CHECK_AND_LOG(sys_ble_char_rx_inject(SYS_BLE_CHR_RUNIT_RX, frame_pca, sizeof(frame_pca)));

  // gpio_esp ADC (device 0, pin 5): read voltage. class 0x01 / packet 0x25.
  packet_sys_io_get_voltage_t adc_read = {.device_id = DEVICE_ID_GPIO_ESP, .pin = 5};
  uint8_t frame_adc[2 + sizeof(adc_read)] = {SYS_CONTRACTS_CLASS_HEADER, HEADER_packet_sys_io_get_voltage_t};
  memcpy(&frame_adc[2], &adc_read, sizeof(adc_read));
  CHECK_AND_LOG(sys_ble_char_rx_inject(SYS_BLE_CHR_RUNIT_RX, frame_adc, sizeof(frame_adc)));

  ESP_LOGI(TAG, "Injection test: frames pushed, check sys_if_rx / dec_sys_contracts logs above for results");
  vTaskDelete(NULL);
}

void runit_start(void) {
  SE_init();
  CHECK_AND_LOG(sys_start_i2c());
  CHECK_AND_LOG(sys_io_static_config());
  CHECK_AND_LOG(sys_power_static_config());
  CHECK_AND_LOG(sys_ble_static_config());

  // Must come after sys_ble_static_config(): the log/error streams reference
  // characteristics and TX slots that it creates.
  CHECK_AND_LOG(SE_configure(&s_runit_error_cfg));

  // Configure ESP GPIO pins 5, 6, 7 as ADC mode
  CHECK_AND_LOG(sys_io_set_mode(DEVICE_ID_GPIO_ESP, 5, SYS_IO_MODE_ADC));
  CHECK_AND_LOG(sys_io_set_mode(DEVICE_ID_GPIO_ESP, 6, SYS_IO_MODE_ADC));
  CHECK_AND_LOG(sys_io_set_mode(DEVICE_ID_GPIO_ESP, 7, SYS_IO_MODE_ADC));

  // Route inbound BLE frames into the packet interface: [class 0xXX][packet 0xYY][payload]
  CHECK_AND_LOG(sys_interface_init());
  // Must come before sys_interface_bind_ble_rx(): class registration and the
  // recording tap are boot-only, not safe against a running RX pump.
  CHECK_AND_LOG(sys_actions_init());
  CHECK_AND_LOG(sys_interface_bind_ble_rx(SYS_BLE_CHR_RUNIT_RX, RUNIT_BLE_RX_FRAME_MAX));

  // Spawn ADC test task
  xTaskCreate(runit_adc_test_task, "adc_test_task", 4096, NULL, 4, NULL);

  // Spawn one-shot injection test (PCA9685 PWM + gpio_esp ADC via the RX buffer)
  xTaskCreate(runit_injection_test_task, "inject_test", 4096, NULL, 4, NULL);

  ESP_LOGI(TAG, "runIT system tasks, BLE interface, and ADC test task initialized successfully");
}
