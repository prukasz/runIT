#pragma once
/******************************************************
Board onboard device creation
!Board rev. 1.0

Bound to sys_actions' boot action (id 0) - see runit.c. Device id / addresses
shall not be changed; devices below are commented out where the chip isn't
populated on this board rev.
****************************************************** */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "device_ads7128.h"
#include "device_ap33772s.h"
#include "device_dac53202.h"
#include "device_gpio_esp.h"
#include "device_ina3221.h"
#include "device_pca9685.h"
#include "device_tca6424a.h"
#include "device_tps55289.h"
#include "runit_board_cfg.h"
#include "runit_board_defs.h"
#include "sys_actions.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_io.h"

/**
 * @brief Boot action (static action id 0): creates onboard devices at boot.
 *
 * Matches action_static_func_t, bound to sys_actions id 0 via
 * sys_actions_bind_static() before sys_actions_init() runs.
 */
err_h runit_at_boot(void* arg) {
  (void)arg;
  SE_ORIGIN_CALL(d_gpio_esp_create(&(d_gpio_esp_cfg_t){
      .device_id = DEVICE_ID_GPIO_ESP,
  }));
  SE_ORIGIN_CALL(d_tca6424a_create(&(d_tca6424a_cfg_t){
      .device_id = DEVICE_ID_TCA6424A,
      .i2c_bus = SYS_I2C_BUS_INTERNAL,
      .i2c_addr = 0x23,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 9, SYS_IO_MODE_INPUT),
      .rst_pin = SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 8, SYS_IO_MODE_OUTPUT_PUSH_PULL),
  }));
  SE_ORIGIN_CALL(d_ads7128_create(&(d_ads7128_cfg_t){
      .device_id = DEVICE_ID_ADS7128,
      .i2c_bus = SYS_I2C_BUS_INTERNAL,
      .i2c_addr = 0x10,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 42, SYS_IO_MODE_INPUT_PULLUP),
      .vref_mv = 20000,
  }));
  // Interrupt test: alert when AIN0 (pin 0) goes above 200 mV. WINDOW_OUTSIDE
  // with a 0 mV low threshold means "outside [0, 200] mV" - since the ADC
  // can't read negative, that only ever fires on the high side. Routed to
  // SYS_CB_ROUTE_IO's dummy logger (sys_io_cb_dummy_log in sys_io.c), so the
  // alert shows up as an "IO event" log line - no own_func needed for this
  // test. Fires on the first sample past the threshold (event_counter=1).
  //
  // 50 mV hysteresis: EVENT_HIGH_FLAG is latched (datasheet 8.3.11) - once
  // set it stays set until cleared, and does NOT self-clear just because a
  // later sample is back in range. With 0 mV hysteresis and a signal that's
  // continuously above 200 mV, the autonomous sequencer (~1 kSPS) re-trips
  // the flag on essentially every conversion - the gap between "cleared" and
  // "set again" collapses to microseconds, far too fast for a falling edge to
  // register as a distinct transition, so ALERT reads as permanently
  // asserted even though it's technically re-tripping continuously. With
  // hysteresis, a re-trip requires the signal to first drop back below
  // (200 - 50) = 150 mV before it can assert again - so it only fires on a
  // genuine crossing, not on every sample of a signal parked above threshold.
  SE_ORIGIN_CALL(sys_io_configure_intr(DEVICE_ID_ADS7128, 0,
      &(sys_io_intr_config_t){
          .mode = SYS_IO_INTR_ADC_WINDOW_OUTSIDE,
          .route_mask = SYS_CB_ROUTE_BIT(SYS_CB_ROUTE_IO),
          .action_mask = 0,
          .adc = {.adc_threshold_up_mV = 200, .adc_threshold_down_mV = 0, .adc_threshold_hysteresis_mV = 100, .adc_event_counter_threshold = 1},
      }));
  ESP_LOGW("board_devices", "ADS7128 AIN0 armed: alert above 200mV (50mV hysteresis)");
  SE_ORIGIN_CALL(d_pca9685_create(&(d_pca9685_cfg_t){
      .device_id = DEVICE_ID_PCA9685, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x60, .oe_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 0, SYS_IO_MODE_OUTPUT_PUSH_PULL)  // rev 1.0: OE not driven by the expander
  }));
  // SE_ORIGIN_CALL(d_dac53202_create(&(d_dac53202_cfg_t){
  //     .device_id = DEVICE_ID_DAC53202,
  //     .i2c_bus = SYS_I2C_BUS_INTERNAL,
  //     .i2c_addr = 0x13,
  // }));
  SE_ORIGIN_CALL(d_tps55289_create(&(d_tps55289_cfg_t){
      .device_id = DEVICE_ID_TPS55289_0,
      .i2c_bus = SYS_I2C_BUS_INTERNAL,
      .i2c_addr = 0x74,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 1, SYS_IO_MODE_INPUT),
      .en_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 17, SYS_IO_MODE_OUTPUT_PUSH_PULL),
  }));
  SE_ORIGIN_CALL(d_tps55289_create(&(d_tps55289_cfg_t){
      .device_id = DEVICE_ID_TPS55289_1,
      .i2c_bus = SYS_I2C_BUS_INTERNAL,
      .i2c_addr = 0x75,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 2, SYS_IO_MODE_INPUT),
      .en_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 16, SYS_IO_MODE_OUTPUT_PUSH_PULL),
  }));
  SE_ORIGIN_CALL(d_ina3221_create(&(d_ina3221_cfg_t){
      .device_id = DEVICE_ID_INA3221,
      .i2c_bus = SYS_I2C_BUS_INTERNAL,
      .i2c_addr = 0x40,
      .crit_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 5, SYS_IO_MODE_INPUT),
      .warn_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 6, SYS_IO_MODE_INPUT),
  }));
  SE_ORIGIN_CALL(d_ap33772s_create(&(d_ap33772s_cfg_t){
      .device_id = DEVICE_ID_AP33772S,
      .i2c_bus = SYS_I2C_BUS_INTERNAL,
      .i2c_addr = 0x52,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 12, SYS_IO_MODE_INPUT),
  }));
  SE_ORIGIN_CALL(sys_io_set_mode(1, 22, SYS_IO_MODE_OUTPUT_PUSH_PULL));
  SE_ORIGIN_CALL(sys_io_set_mode(1, 23, SYS_IO_MODE_OUTPUT_PUSH_PULL));
  ESP_LOGI("board_devices", "onboard devices created");

  // runit_test_pca9685_start();  // uncomment to run the PCA9685 error-handling test (see below)
  return NULL;
}

/**
 * @brief One-shot diagnostic test for the sys_device error-handling scheme
 * (see SYS_DEVICE.MD's "Per-Instance Error Handling" and
 * adapter_pca9685.c's device_error_handler()). Walks through most of the
 * error tags a PCA9685 call can hit, ending with a real driver call retried
 * every second for 10s - disconnect the PCA9685 during that window to force
 * an I2C failure, which is the one path that reaches ERR_DEV_DEP_FAILED and
 * therefore device_error_handler()'s root-cause explanation. Not wired into
 * boot; call runit_test_pca9685_start() manually (see the commented call
 * above) when you want to run it.
 */
static void runit_test_pca9685_task(void* arg) {
  (void)arg;
  static const char* TTAG = "pca_test";
  vTaskDelay(pdMS_TO_TICKS(1000));
  ESP_LOGW(TTAG, "=== PCA9685 error test start ===");

  // Opt into use_error_handler so device_error_handler() actually runs -
  // actions {1,2,3} are placeholders, nothing is bound to them yet.
  sys_device_set_error_handling(DEVICE_ID_PCA9685, true, false, (uint8_t[3]){1, 2, 3});

  ESP_LOGW(TTAG, "-- 1/6: out-of-range pin -> ERR_IO_PIN_UNAVAILABLE --");
  SE_ORIGIN_CALL(sys_io_toggle(DEVICE_ID_PCA9685, 99));
  vTaskDelay(pdMS_TO_TICKS(500));

  ESP_LOGW(TTAG, "-- 2/6: unsupported op (set_mode is NULL on this device) -> ERR_DEV_FEATURE_UNAVAILABLE --");
  SE_ORIGIN_CALL(sys_io_set_mode(DEVICE_ID_PCA9685, 0, SYS_IO_MODE_INPUT));
  vTaskDelay(pdMS_TO_TICKS(500));

  ESP_LOGW(TTAG, "-- 3/6: unsupported op (get_voltage is NULL on this device) -> ERR_DEV_FEATURE_UNAVAILABLE --");
  uint32_t mv = 0;
  SE_ORIGIN_CALL(sys_io_get_voltage(DEVICE_ID_PCA9685, 0, &mv));
  vTaskDelay(pdMS_TO_TICKS(500));

  ESP_LOGW(TTAG, "-- 4/6: unregistered device id -> ERR_DEV_NOT_FOUND --");
  SE_ORIGIN_CALL(sys_io_toggle(99, 0));
  vTaskDelay(pdMS_TO_TICKS(500));

  ESP_LOGW(TTAG, "-- 5/6: suspended device -> ERR_DEV_SUSPENDED --");
  sys_device_suspend(DEVICE_ID_PCA9685);
  SE_ORIGIN_CALL(sys_io_toggle(DEVICE_ID_PCA9685, 0));
  sys_device_resume(DEVICE_ID_PCA9685);
  vTaskDelay(pdMS_TO_TICKS(500));

  ESP_LOGW(TTAG, "-- 6/6: real driver call, retried for 10s -> DISCONNECT PCA9685 NOW to force ERR_DEV_DEP_FAILED --");
  for (int i = 0; i < 10; i++) {
    err_h err = sys_io_toggle(DEVICE_ID_PCA9685, 0);
    bool failed = SE_IS_ERR(err);
    // Push before logging: the handler task runs at a higher priority (5)
    // than this test task (4), so the trace/root-cause lines below preempt
    // and print before the "attempt" line that follows.
    SE_ORIGIN_CALL(err);
    ESP_LOGW(TTAG, "  toggle attempt %d/10: %s", i + 1, failed ? "failed - see error trace above" : "ok");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  ESP_LOGW(TTAG, "=== PCA9685 error test done ===");
  vTaskDelete(NULL);
}

static inline void runit_test_pca9685_start(void) {
  xTaskCreate(runit_test_pca9685_task, "pca_err_test", 4096, NULL, 4, NULL);
}
