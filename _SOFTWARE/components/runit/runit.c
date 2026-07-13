#include "runit.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "runit_board_cfg.h"
#include "status.h"
#include "status_codes.h"
#include "sys_ble.h"
#include "sys_interface.h"
#include "sys_io.h"
#include "sys_device.h"
#include "esp_timer.h"
#include "sys_h_bridge.h"
#include "device_drv8962.h"

static const char* TAG = "runit_test";

static void test_input_callback(const sys_io_intr_event_t* event) { ESP_LOGI(TAG, "ISR Callback: Pin %d triggered by mode %d", event->pin_num, event->triggered_by); }

static void test_adc_callback(const sys_io_intr_event_t* event) { ESP_LOGI(TAG, "ISR Callback: ADC Pin %d triggered threshold event by mode %d", event->pin_num, event->triggered_by); }

static void test_current_callback(uint8_t device_id, sys_power_events_e triggered_by) {
  ESP_LOGI(TAG, "IPROPI Alert callback: Device %u triggered event %d", device_id, triggered_by);
}

static void gpio_esp_test_task(void* arg) {
  vTaskDelay(pdMS_TO_TICKS(1000));  // Wait for initialization to stabilize
  ESP_LOGI(TAG, "GPIO ESP test task started");

  // 1. Initialize IO Subsystem
  status_rep_t io_status = sys_start_io();
  if (STA_IS_ERR(io_status)) {
    ESP_LOGE(TAG, "Failed to start IO subsystem");
    vTaskDelete(NULL);
    return;
  }

  // 2. Set mode for ADC pins 4, 5, 6, 7
  ESP_LOGI(TAG, "Configuring ADC pins 4, 5, 6, 7");
  sys_io_set_mode(DEVICE_ID_GPIO_ESP, 4, SYS_IO_MODE_ADC);
  sys_io_set_mode(DEVICE_ID_GPIO_ESP, 5, SYS_IO_MODE_ADC);
  sys_io_set_mode(DEVICE_ID_GPIO_ESP, 6, SYS_IO_MODE_ADC);
  sys_io_set_mode(DEVICE_ID_GPIO_ESP, 7, SYS_IO_MODE_ADC);

  // 3. Set mode for input pin 9 and output pin 10
  ESP_LOGI(TAG, "Configuring input pin 9 and output pin 10");
  sys_io_set_mode(DEVICE_ID_GPIO_ESP, 9, SYS_IO_MODE_INPUT_PULLUP);
  sys_io_set_mode(DEVICE_ID_GPIO_ESP, 10, SYS_IO_MODE_OUTPUT_PUSH_PULL);

  // 4. Attach interrupts
  sys_io_intr_config_t input_cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE, .callback = test_input_callback, .user_ctx = NULL};
  sys_io_configure_intr(DEVICE_ID_GPIO_ESP, 9, &input_cfg);

  sys_io_intr_config_t adc_cfg = {.mode = SYS_IO_INTR_ADC_WINDOW_OUTSIDE, .callback = test_adc_callback, .user_ctx = NULL, .adc = {.adc_threshold_up_mV = 2500, .adc_threshold_down_mV = 1000, .adc_threshold_hysteresis_mV = 500, .adc_event_counter_threshold = 1}};
  sys_io_configure_intr(DEVICE_ID_GPIO_ESP, 4, &adc_cfg);
  sys_io_configure_intr(DEVICE_ID_GPIO_ESP, 5, &adc_cfg);
  sys_io_configure_intr(DEVICE_ID_GPIO_ESP, 6, &adc_cfg);
  sys_io_configure_intr(DEVICE_ID_GPIO_ESP, 7, &adc_cfg);

  // 4.5. Install DRV8962 driver
  ESP_LOGI(TAG, "Installing DRV8962 device...");
  drv8962_config_t drv_cfg = {
    .in_devices = {DEVICE_ID_GPIO_ESP, DEVICE_ID_GPIO_ESP, DEVICE_ID_GPIO_ESP, DEVICE_ID_GPIO_ESP},
    .in_pins = {11, 12, 13, 14},
    .en_devices = {DEVICE_ID_GPIO_ESP, DEVICE_ID_GPIO_ESP, DEVICE_ID_GPIO_ESP, DEVICE_ID_GPIO_ESP},
    .en_pins = {15, 16, 17, 18},
    .nsleep_device = DEVICE_ID_GPIO_ESP,
    .nsleep_pin = 19,
    .nfault_device = DEVICE_ID_GPIO_ESP,
    .nfault_pin = 20,
    .mode_device = DEVICE_ID_GPIO_ESP,
    .mode_pin = 21,
    .mode_val = true,
    .ocpm_device = DEVICE_ID_GPIO_ESP,
    .ocpm_pin = 22,
    .ocpm_val = true,
    .r_ipropi_ohms = {3090, 3090, 3090, 3090}
  };

  status_rep_t install_status = d_drv8962_create(2, &drv_cfg);
  if (STA_IS_ERR(install_status)) {
    ESP_LOGE(TAG, "Failed to install DRV8962: code=%d", install_status.e_code);
  } else {
    // Add current limit callback on channel 0 (which triggers at 1.0A)
    sys_power_monitor_add_callback(2, 0, 1000, SYS_PWR_EVENT_OCP_CRITICAL, test_current_callback);
  }

  // 5. Measurement and read loop with advanced state transitions
  int cycle_count = 0;
  while (1) {
    uint32_t mv4 = 0, mv5 = 0, mv6 = 0, mv7 = 0;
    bool lvl9 = false;

    // Time GPIO Read
    uint64_t start_gpio = esp_timer_get_time();
    sys_io_get_level(DEVICE_ID_GPIO_ESP, 9, &lvl9);
    uint64_t end_gpio = esp_timer_get_time();
    uint64_t diff_gpio = end_gpio - start_gpio;

    // Time ADC Read
    uint64_t start_adc = esp_timer_get_time();
    sys_io_get_voltage(DEVICE_ID_GPIO_ESP, 4, &mv4);
    uint64_t end_adc = esp_timer_get_time();
    uint64_t diff_adc = end_adc - start_adc;

    sys_io_get_voltage(DEVICE_ID_GPIO_ESP, 5, &mv5);
    sys_io_get_voltage(DEVICE_ID_GPIO_ESP, 6, &mv6);
    sys_io_get_voltage(DEVICE_ID_GPIO_ESP, 7, &mv7);

    ESP_LOGI(TAG, "Reads: Pin4=%lumV (took %lluus), Pin5=%lumV, Pin6=%lumV, Pin7=%lumV | Pin9=%d (took %lluus)",
             mv4, diff_adc, mv5, mv6, mv7, lvl9, diff_gpio);

    // Toggle output pin 10
    sys_io_toggle(DEVICE_ID_GPIO_ESP, 10);
    bool lvl10 = false;
    sys_io_get_level(DEVICE_ID_GPIO_ESP, 10, &lvl10);
    ESP_LOGI(TAG, "Toggled output Pin10 to level: %d", lvl10);

    // Toggle/drive DRV8962 motor outputs
    static uint16_t motor_duty = 10000;
    static bool motor_forward = true;

    if (motor_forward) {
      sys_h_bridge_forward(2, SYS_H_BRIDGE_MODE_NORMAL, motor_duty, 0);
      ESP_LOGI(TAG, "Motor 0: driving forward (duty: %u)", motor_duty);
    } else {
      sys_h_bridge_backwards(2, SYS_H_BRIDGE_MODE_NORMAL, motor_duty, 0);
      ESP_LOGI(TAG, "Motor 0: driving backwards (duty: %u)", motor_duty);
    }

    // Also get current
    int32_t current_ma = 0;
    sys_power_monitor_get_current(2, 0, &current_ma);
    ESP_LOGI(TAG, "Motor 0 current: %ld mA", current_ma);

    motor_duty += 5000;
    if (motor_duty > 60000) {
      motor_duty = 10000;
      motor_forward = !motor_forward;
    }

    cycle_count++;

    // Demonstrate freeze/sync every 5 iterations
    if (cycle_count == 5) {
      ESP_LOGI(TAG, ">>> FREEZING DEVICE (testing double buffering) <<<");
      sys_device_freeze(DEVICE_ID_GPIO_ESP);
      
      // Let's toggle Pin 10 and read inputs while frozen
      for (int f = 0; f < 3; f++) {
        uint32_t f_mv4 = 0, f_mv5 = 0, f_mv6 = 0, f_mv7 = 0;
        bool f_lvl9 = false;

        sys_io_get_voltage(DEVICE_ID_GPIO_ESP, 4, &f_mv4);
        sys_io_get_voltage(DEVICE_ID_GPIO_ESP, 5, &f_mv5);
        sys_io_get_voltage(DEVICE_ID_GPIO_ESP, 6, &f_mv6);
        sys_io_get_voltage(DEVICE_ID_GPIO_ESP, 7, &f_mv7);
        sys_io_get_level(DEVICE_ID_GPIO_ESP, 9, &f_lvl9);

        sys_io_toggle(DEVICE_ID_GPIO_ESP, 10);
        sys_io_get_level(DEVICE_ID_GPIO_ESP, 10, &lvl10);

        ESP_LOGI(TAG, "[FROZEN] Reads: Pin4=%lumV, Pin5=%lumV, Pin6=%lumV, Pin7=%lumV | Pin9=%d | Toggled Pin10 (cached): %d",
                 f_mv4, f_mv5, f_mv6, f_mv7, f_lvl9, lvl10);
        vTaskDelay(pdMS_TO_TICKS(2000));
      }

      ESP_LOGI(TAG, ">>> SYNCHRONIZING & UNFREEZING DEVICE (applying buffered updates) <<<");
      sys_device_sync(DEVICE_ID_GPIO_ESP);
    }

    // Demonstrate reset and re-initialization under various conditions every 10 iterations
    if (cycle_count == 10) {
      ESP_LOGI(TAG, "================================================");
      ESP_LOGI(TAG, ">>> STARTING GPIO RESET FUNCTIONALITY TESTS <<<");
      ESP_LOGI(TAG, "================================================");

      // 4.1. Reset unconfigured Pin 11
      ESP_LOGI(TAG, "[Test 4.1] Resetting unconfigured Pin 11...");
      status_rep_t r11 = sys_io_reset(DEVICE_ID_GPIO_ESP, 11);
      ESP_LOGI(TAG, "Result of resetting unconfigured Pin 11: code=%d", r11.e_code);

      // 4.2. Reset digital input Pin 9 in normal mode
      ESP_LOGI(TAG, "[Test 4.2] Resetting configured digital input Pin 9...");
      status_rep_t r9 = sys_io_reset(DEVICE_ID_GPIO_ESP, 9);
      ESP_LOGI(TAG, "Result of resetting Pin 9: code=%d", r9.e_code);
      // Verify Pin 9 is no longer available
      bool val9 = false;
      status_rep_t g9 = sys_io_get_level(DEVICE_ID_GPIO_ESP, 9, &val9);
      ESP_LOGI(TAG, "Attempting read on reset Pin 9: code=%d (should fail)", g9.e_code);

      // 4.3. Reset ADC Pin 4 in normal mode
      ESP_LOGI(TAG, "[Test 4.3] Resetting configured ADC Pin 4...");
      status_rep_t r4 = sys_io_reset(DEVICE_ID_GPIO_ESP, 4);
      ESP_LOGI(TAG, "Result of resetting Pin 4: code=%d", r4.e_code);
      // Verify Pin 4 is no longer available
      uint32_t mv4 = 0;
      status_rep_t g4 = sys_io_get_voltage(DEVICE_ID_GPIO_ESP, 4, &mv4);
      ESP_LOGI(TAG, "Attempting read on reset Pin 4: code=%d (should fail)", g4.e_code);

      // 4.4. Reset digital output Pin 10 while frozen
      ESP_LOGI(TAG, "[Test 4.4] Freezing device and resetting output Pin 10...");
      sys_device_freeze(DEVICE_ID_GPIO_ESP);
      status_rep_t r10 = sys_io_reset(DEVICE_ID_GPIO_ESP, 10);
      ESP_LOGI(TAG, "Result of resetting Pin 10 while frozen: code=%d", r10.e_code);
      ESP_LOGI(TAG, "Syncing and unfreezing device...");
      sys_device_sync(DEVICE_ID_GPIO_ESP);

      // 4.5. Reset ADC Pin 5 while suspended
      ESP_LOGI(TAG, "[Test 4.5] Suspending device and resetting ADC Pin 5...");
      sys_device_suspend(DEVICE_ID_GPIO_ESP);
      status_rep_t r5 = sys_io_reset(DEVICE_ID_GPIO_ESP, 5);
      ESP_LOGI(TAG, "Result of resetting Pin 5 while suspended: code=%d", r5.e_code);
      ESP_LOGI(TAG, "Resuming device...");
      sys_device_resume(DEVICE_ID_GPIO_ESP);

      // 4.6. Re-configure the reset pins to restore normal operation
      ESP_LOGI(TAG, ">>> Re-configuring reset pins for normal operation <<<");
      sys_io_set_mode(DEVICE_ID_GPIO_ESP, 4, SYS_IO_MODE_ADC);
      sys_io_set_mode(DEVICE_ID_GPIO_ESP, 5, SYS_IO_MODE_ADC);
      sys_io_set_mode(DEVICE_ID_GPIO_ESP, 9, SYS_IO_MODE_INPUT_PULLUP);
      sys_io_set_mode(DEVICE_ID_GPIO_ESP, 10, SYS_IO_MODE_OUTPUT_PUSH_PULL);
      
      // Re-configure interrupts
      sys_io_configure_intr(DEVICE_ID_GPIO_ESP, 9, &input_cfg);
      sys_io_configure_intr(DEVICE_ID_GPIO_ESP, 4, &adc_cfg);
      sys_io_configure_intr(DEVICE_ID_GPIO_ESP, 5, &adc_cfg);

      ESP_LOGI(TAG, "================================================");
      ESP_LOGI(TAG, ">>> GPIO RESET FUNCTIONALITY TESTS COMPLETED <<<");
      ESP_LOGI(TAG, "================================================");
      cycle_count = 0; // Reset cycle counter
    }

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

static void runit_ble_task(void* arg) {
  ESP_LOGI("runit_ble", "BLE RUNIT Task Started");
  sys_start_ble();
  while (1) {
    sys_interface_decode(NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void runit_start(void) {
  xTaskCreate(runit_ble_task, "runit_ble_task", 4096, NULL, 5, NULL);
  xTaskCreate(gpio_esp_test_task, "gpio_esp_test", 4096, NULL, 5, NULL);
}
