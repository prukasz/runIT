#include "runit_board_cfg.h"

#include <esp_log.h>
#include <sdkconfig.h>
#include "devices.h"
#include "runit_board_defs.h"
#include "sys_ble.h"
#include "sys_ble_provider.h"
#include "sys_uart_provider.h"
#include "sys_buffers.h"
#include "sys_data_connector.h"
#include "sys_error_log.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "runit.h"
#include "sys_power.h"
#include "sys_actions.h"
#include "sys_actions_static.h"

#define TAG "board_configuration"
#define OWNER OWNER_RUNIT_BOARD

/* Bring-up switches: which onboard I2C devices boot action 1 installs (the ESP
   GPIO device always is). In-file on purpose, not Kconfig: toggling one
   rebuilds this file only. All 1 on a finished board, all 0 on a bare devkit.
   PCA9685, TPS55289, INA3221 and AP33772S use TCA6424A pins. */
#define RUNIT_BOARD_DEV_TCA6424A 1
#define RUNIT_BOARD_DEV_ADS7128 1
#define RUNIT_BOARD_DEV_PCA9685 1
#define RUNIT_BOARD_DEV_TPS55289 1
#define RUNIT_BOARD_DEV_INA3221 1
#define RUNIT_BOARD_DEV_AP33772S 1
#define RUNIT_BOARD_DEV_DAC53202 1
/* DRV8962 #1 (ESP GPIO): pins confirmed by the legacy firmware header, but
   the ESP GPIO device has no PWM yet (set_pwm_* = NULL), so its install would
   fail. Off until the ESP adapter drives PWM (LEDC). */
#define RUNIT_BOARD_DEV_DRV8962_0 0
#define RUNIT_BOARD_DEV_DRV8962_1 1

/* H-bridge PWM on the PCA9685: its one frequency is shared with the user
   channels 0-7; the chip tops out at ~1.5 kHz. */
#define RUNIT_BOARD_DRV8962_PCA_PWM_HZ 1000
/* Chopping threshold at start (VREF from the DAC), per chip. */
#define RUNIT_BOARD_DRV8962_LIMIT_MA 2000

/* TEMP bring-up: budget for a source whose current limit is unknown (normally
   RUNIT_BOARD_POWER_UNKNOWN_SOURCE_MA). Raised so the unloaded rails can go to
   10 V on a plain 5 V USB feed. 0 = use the board default. */
#define RUNIT_BOARD_TEST_UNKNOWN_SOURCE_MA 0

err_h runit_board_bind_boot_action(void) {
  return sys_actions_bind_static(CONFIG_SYS_ACTION_ID_BOOT, runit_board_devices_init);
}

err_h runit_board_invoke_boot_action(void) {
  return sys_actions_invoke(SYS_ACTION_SCOPE_STATIC, CONFIG_SYS_ACTION_ID_BOOT);
}

err_h runit_board_i2c_init(void) {
  i2c_master_bus_config_t bus0_cfg = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = SYS_PIN_I2C_0_SDA,
      .scl_io_num = SYS_PIN_I2C_0_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = SYS_I2C_PULLUP_ENABLE,
  };
  i2c_master_bus_config_t bus1_cfg = {
      .i2c_port = I2C_NUM_1,
      .sda_io_num = SYS_PIN_I2C_1_SDA,
      .scl_io_num = SYS_PIN_I2C_1_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = SYS_I2C_PULLUP_ENABLE,
  };
  return sys_i2c_init(&bus0_cfg, &bus1_cfg);
}

/* Power layout of this board revision. Both TPS55289 rails are budgeted;
   the INA3221 measures them and the board input. */
static const sys_power_consumer_t s_power_consumers[] = {
    {.vreg_id = DEVICE_ID_TPS55289_0, .monitor = {.device_id = DEVICE_ID_INA3221, .channel = RUNIT_BOARD_INA_CH_RAIL_A}},
    {.vreg_id = DEVICE_ID_TPS55289_1, .monitor = {.device_id = DEVICE_ID_INA3221, .channel = RUNIT_BOARD_INA_CH_RAIL_B}},
};

/* Default response per power event; the app can change them at runtime. */
static const uint8_t s_power_responses[SYS_PWR_EVENT_COUNT] = {
    [SYS_PWR_EVENT_OVP] = SYS_POWER_RESPONSE_DISABLE,
    [SYS_PWR_EVENT_UVP] = SYS_POWER_RESPONSE_NOTIFY,
    [SYS_PWR_EVENT_SPC] = SYS_POWER_RESPONSE_DISABLE,
    [SYS_PWR_EVENT_OCP_WARNING] = SYS_POWER_RESPONSE_NOTIFY,
    [SYS_PWR_EVENT_OCP_CRITICAL] = SYS_POWER_RESPONSE_DISABLE,
    [SYS_PWR_EVENT_OTP] = SYS_POWER_RESPONSE_SAFE_STATE,
    [SYS_PWR_EVENT_BATTERY_LOW] = SYS_POWER_RESPONSE_NOTIFY,
    [SYS_PWR_EVENT_BATTERY_CRITICAL] = SYS_POWER_RESPONSE_SAFE_STATE,
    [SYS_PWR_EVENT_BUDGET_EXCEEDED] = SYS_POWER_RESPONSE_DISABLE,
    [SYS_PWR_EVENT_SOURCE_CHANGED] = SYS_POWER_RESPONSE_NOTIFY,
};

static const sys_power_board_t s_power_board = {
    .max_mV = RUNIT_BOARD_POWER_LIMIT_MV,
    .max_mA = RUNIT_BOARD_POWER_LIMIT_MA,
    .reserve_mW = RUNIT_BOARD_POWER_RESERVE_MW,
    .unknown_source_mA = RUNIT_BOARD_TEST_UNKNOWN_SOURCE_MA ? RUNIT_BOARD_TEST_UNKNOWN_SOURCE_MA : RUNIT_BOARD_POWER_UNKNOWN_SOURCE_MA,
    .vreg_efficiency_pct = RUNIT_BOARD_VREG_EFFICIENCY_PCT,
    .input = {.device_id = DEVICE_ID_INA3221, .channel = RUNIT_BOARD_INA_CH_INPUT},
    .usb_pd_id = DEVICE_ID_AP33772S,
    // TODO: source indicator pins. The eFuse power-good outputs are TCA 3
    // (VUSB_OK) and TCA 4 (VEXT_OK), but VUSB_OK is also high on plain 5 V USB
    // (not only PD), and the manager starts before the TCA6424A is installed.
    // Until then the source is inferred: USB-PD if it offers power, else an
    // entered PSU, else a battery.
    .source_pins = NULL,
    .source_pin_count = 0,
    .consumers = s_power_consumers,
    .consumer_count = sizeof(s_power_consumers) / sizeof(s_power_consumers[0]),
    .responses = s_power_responses,
};

static err_h runit_power_safe_state(void) {
  runit_enter_safe_state();
  return NULL;
}

err_h runit_board_power_init(void) {
  sys_power_register_safe_state(runit_power_safe_state);
  SE_TRY(sys_power_init(&s_power_board));
  ESP_LOGI(TAG, "power manager started");
  return NULL;
}

err_h runit_board_ble_init(void) {
  sys_ble_svc_cfg_t service_cfg = {.uuid = SYS_BLE_SVC_RUNIT, .is_primary = true};
  SE_TRY(sys_ble_service_create(&service_cfg));

  /* Buffers are sized in frames of CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX, not
     bytes: a no-split ringbuffer takes items of only half its size. */
  sys_ble_char_cfg_t rx_cfg = {.uuid = SYS_BLE_CHR_RUNIT_RX, .is_write = true, .desc = "runit RX",
                               .rx_buffer_size = SYS_BUFF_SIZE_FOR(CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX, 2)};
  SE_TRY(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &rx_cfg));
  sys_ble_char_cfg_t tx_cfg = {.uuid = SYS_BLE_CHR_RUNIT_TX, .is_notify = true, .desc = "runit TX",
                               .tx_buffer_size = SYS_BUFF_SIZE_FOR(CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX, 4)};
  SE_TRY(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &tx_cfg));
  sys_ble_char_cfg_t status_cfg = {.uuid = SYS_BLE_CHT_RUNIT_STATUS, .is_notify = true, .desc = "runit Status", .tx_buffer_size = 512};
  SE_TRY(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &status_cfg));
  sys_ble_char_cfg_t logs_cfg = {.uuid = SYS_BLE_CHR_RUNIT_LOGS, .is_notify = true, .desc = "runit LOGS",
                                 .tx_buffer_size = SYS_BUFF_SIZE_FOR(CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX, 4)};
  SE_TRY(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &logs_cfg));

  SE_TRY(sys_ble_database_sync());
  ESP_LOGI(TAG, "BLE initialized");
  return NULL;
}

err_h runit_board_connector_bindings_init(void) {
  const uint8_t ble = RUNIT_DATA_PROVIDER_BLE;
  SE_TRY(sys_ble_provider_register(ble));
  SE_TRY(sys_data_connector_bind_tx(SYS_DATA_CONNECTOR_LOGS, ble, SYS_BLE_CHR_RUNIT_LOGS));
  SE_TRY(sys_data_connector_bind_tx(SYS_DATA_CONNECTOR_ERRORS, ble, SYS_BLE_CHR_RUNIT_LOGS));
  SE_TRY(sys_data_connector_bind_tx(SYS_DATA_CONNECTOR_TELEMETRY, ble, SYS_BLE_CHR_RUNIT_TX));
  SE_TRY(sys_data_connector_bind_tx(SYS_DATA_CONNECTOR_INTERFACE, ble, SYS_BLE_CHR_RUNIT_TX));
  SE_TRY(sys_data_connector_bind_rx(SYS_DATA_CONNECTOR_INTERFACE, ble, SYS_BLE_CHR_RUNIT_RX));
  ESP_LOGI(TAG, "BLE data connectors bound");

#if CONFIG_RUNIT_UART_CONSOLE_LINK
  /* Console UART: commands in, responses / telemetry / error packets out. The
     text log is already on the console (mirrored), so the logs connector isn't bound. */
  const uint8_t uart = RUNIT_DATA_PROVIDER_UART;
  SE_TRY(sys_uart_provider_register(uart));
  SE_TRY(sys_data_connector_bind_tx(SYS_DATA_CONNECTOR_ERRORS, uart, SYS_UART_ENDPOINT_CONSOLE));
  SE_TRY(sys_data_connector_bind_tx(SYS_DATA_CONNECTOR_TELEMETRY, uart, SYS_UART_ENDPOINT_CONSOLE));
  SE_TRY(sys_data_connector_bind_tx(SYS_DATA_CONNECTOR_INTERFACE, uart, SYS_UART_ENDPOINT_CONSOLE));
  SE_TRY(sys_data_connector_bind_rx(SYS_DATA_CONNECTOR_INTERFACE, uart, SYS_UART_ENDPOINT_CONSOLE));
  ESP_LOGI(TAG, "UART console data connectors bound");
#endif
  return NULL;
}

/* sys_errors output: text lines to the logs connector, binary error packets
   to the errors connector. A failed send is released, not reported: reporting
   it would log, and the log comes straight back to this sink. A log line longer
   than the transport's current limit is cut to it (text only; error packets
   are sized by runit_error_packet_max_len instead). */
static void runit_error_log_send(const void* data, size_t len) {
  size_t max = sys_data_connector_max_payload(SYS_DATA_CONNECTOR_LOGS);
  SE_release(sys_data_connector_send(SYS_DATA_CONNECTOR_LOGS, data, len < max ? len : max));
}

static void runit_error_packet_send(const void* data, size_t len) {
  SE_release(sys_data_connector_send(SYS_DATA_CONNECTOR_ERRORS, data, len));
}

static size_t runit_error_packet_max_len(void) {
  return sys_data_connector_max_payload(SYS_DATA_CONNECTOR_ERRORS);
}

err_h runit_board_error_sink_init(void) {
  SE_register_sink(&(sys_error_sink_t){
      .send_log = runit_error_log_send,
      .send_packet = runit_error_packet_send,
      .packet_max_len = runit_error_packet_max_len,
  });
  return NULL;
}

/* Install one onboard device and mark it, so users can't uninstall it. */
#define RUNIT_BOARD_DEVICE(device_id, create_call)    \
  do {                                                \
    SE_TRY(create_call);                       \
    SE_TRY(sys_device_set_onboard(device_id)); \
  } while (0)

err_h runit_board_devices_init(void) {
  RUNIT_BOARD_DEVICE(DEVICE_ID_GPIO_ESP, d_gpio_esp_create(&(d_gpio_esp_cfg_t){.device_id = DEVICE_ID_GPIO_ESP}));
#if RUNIT_BOARD_DEV_TCA6424A
  RUNIT_BOARD_DEVICE(DEVICE_ID_TCA6424A, d_tca6424a_create(&(d_tca6424a_cfg_t){
      .device_id = DEVICE_ID_TCA6424A, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x23,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 9, SYS_IO_MODE_INPUT),
      .rst_pin = SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 8, SYS_IO_MODE_OUTPUT_PUSH_PULL),
  }));
  /* Status LEDs (high = on). */
  SE_TRY(sys_io_set_mode(SYS_IO_PIN(DEVICE_ID_TCA6424A, 22, SYS_IO_MODE_OUTPUT_PUSH_PULL)));
  SE_TRY(sys_io_set_mode(SYS_IO_PIN(DEVICE_ID_TCA6424A, 23, SYS_IO_MODE_OUTPUT_PUSH_PULL)));
  /* LM73100 H-bridge supply switches (high = on), all off at boot. Each
     DRV8962 has one VM node fed by two switches - turn on one at a time. */
  for (uint8_t pin = RUNIT_BOARD_TCA_DRV2_VM_RAIL_A; pin <= RUNIT_BOARD_TCA_DRV1_VM_RAIL_B; pin++) {
    SE_TRY(sys_io_set_mode(SYS_IO_PIN(DEVICE_ID_TCA6424A, pin, SYS_IO_MODE_OUTPUT_PUSH_PULL)));
    SE_TRY(sys_io_set_level(SYS_IO_PIN(DEVICE_ID_TCA6424A, pin, SYS_IO_MODE_OUTPUT_PUSH_PULL), false));
  }
#endif
#if RUNIT_BOARD_DEV_ADS7128
  RUNIT_BOARD_DEVICE(DEVICE_ID_ADS7128, d_ads7128_create(&(d_ads7128_cfg_t){
      .device_id = DEVICE_ID_ADS7128, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x10,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 42, SYS_IO_MODE_INPUT_PULLUP), .vref_mV = 20000,
  }));
#endif
#if RUNIT_BOARD_DEV_PCA9685
  RUNIT_BOARD_DEVICE(DEVICE_ID_PCA9685, d_pca9685_create(&(d_pca9685_cfg_t){
      .device_id = DEVICE_ID_PCA9685, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x60,
      .oe_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 0, SYS_IO_MODE_OUTPUT_PUSH_PULL),
  }));
#endif
#if RUNIT_BOARD_DEV_TPS55289
  RUNIT_BOARD_DEVICE(DEVICE_ID_TPS55289_0, d_tps55289_create(&(d_tps55289_cfg_t){
      .device_id = DEVICE_ID_TPS55289_0, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x74,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 1, SYS_IO_MODE_INPUT),
      .en_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 17, SYS_IO_MODE_OUTPUT_PUSH_PULL),
  }));
  RUNIT_BOARD_DEVICE(DEVICE_ID_TPS55289_1, d_tps55289_create(&(d_tps55289_cfg_t){
      .device_id = DEVICE_ID_TPS55289_1, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x75,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 2, SYS_IO_MODE_INPUT),
      .en_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 16, SYS_IO_MODE_OUTPUT_PUSH_PULL),
  }));
#endif
#if RUNIT_BOARD_DEV_INA3221
  RUNIT_BOARD_DEVICE(DEVICE_ID_INA3221, d_ina3221_create(&(d_ina3221_cfg_t){
      .device_id = DEVICE_ID_INA3221, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x40,
      .crit_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 5, SYS_IO_MODE_INPUT),
      .warn_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 6, SYS_IO_MODE_INPUT),
      .inverted_mask = 0x07,  // all three shunts reversed by design (input and both rails)
  }));
#endif
#if RUNIT_BOARD_DEV_AP33772S
  RUNIT_BOARD_DEVICE(DEVICE_ID_AP33772S, d_ap33772s_create(&(d_ap33772s_cfg_t){
      .device_id = DEVICE_ID_AP33772S, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x52,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 21, SYS_IO_MODE_INPUT),
  }));
#endif
#if RUNIT_BOARD_DEV_DAC53202
  /* A0 to AGND (0x48, probed on the PCB 2026-09-24). OUT0 / OUT1 = VREF of DRV8962 #1 / #2. */
  RUNIT_BOARD_DEVICE(DEVICE_ID_DAC53202, d_dac53202_create(&(d_dac53202_cfg_t){
      .device_id = DEVICE_ID_DAC53202, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x48,
  }));
#endif
#if RUNIT_BOARD_DEV_DRV8962_0
  /* DRV8962 #1: IN / EN on ESP GPIO, IPROPI on ESP ADC, nSLEEP / nFAULT on the
     TCA6424A (9 / 11), VREF from DAC OUT0. VM: TCA 14 (VSUP) or 15 (rail B). */
  RUNIT_BOARD_DEVICE(DEVICE_ID_DRV8962_0, d_drv8962_create(&(d_drv8962_cfg_t){
      .device_id = DEVICE_ID_DRV8962_0, .topology = DRV8962_TOPOLOGY_2_FULL_BRIDGES,
      .in_pins = {SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 21, SYS_IO_MODE_PWM), SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 47, SYS_IO_MODE_PWM),
                  SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 48, SYS_IO_MODE_PWM), SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 45, SYS_IO_MODE_PWM)},
      .en_pins = {SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 38, SYS_IO_MODE_OUTPUT_PUSH_PULL), SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 39, SYS_IO_MODE_OUTPUT_PUSH_PULL),
                  SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 2, SYS_IO_MODE_OUTPUT_PUSH_PULL), SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 1, SYS_IO_MODE_OUTPUT_PUSH_PULL)},
      .current_adc_pins = {SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 7, SYS_IO_MODE_ADC), SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 6, SYS_IO_MODE_ADC),
                           SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 5, SYS_IO_MODE_ADC), SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 4, SYS_IO_MODE_ADC)},
      .nsleep_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 9, SYS_IO_MODE_OUTPUT_PUSH_PULL),
      .nfault_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 11, SYS_IO_MODE_INPUT),
      .vref_dac_pin = SYS_IO_PIN_INIT(DEVICE_ID_DAC53202, 0, SYS_IO_MODE_DAC),
      .current_limit_mA = {RUNIT_BOARD_DRV8962_LIMIT_MA, RUNIT_BOARD_DRV8962_LIMIT_MA, RUNIT_BOARD_DRV8962_LIMIT_MA, RUNIT_BOARD_DRV8962_LIMIT_MA},
      .turn_off_at_ocp = true,
  }));
#endif
#if RUNIT_BOARD_DEV_DRV8962_1
  /* DRV8962 #2: IN1-4 = PCA9685 CH 8-11, EN1-4 = CH 12-15. IPROPI has
     resistors only (no ADC); nSLEEP / nFAULT on the TCA6424A (8 / 10), VREF
     from DAC OUT1. VM: TCA 12 (rail A) or 13 (VSUP). */
  RUNIT_BOARD_DEVICE(DEVICE_ID_DRV8962_1, d_drv8962_create(&(d_drv8962_cfg_t){
      .device_id = DEVICE_ID_DRV8962_1, .topology = DRV8962_TOPOLOGY_2_FULL_BRIDGES,
      .in_pins = {SYS_IO_PIN_INIT(DEVICE_ID_PCA9685, 8, SYS_IO_MODE_PWM), SYS_IO_PIN_INIT(DEVICE_ID_PCA9685, 9, SYS_IO_MODE_PWM),
                  SYS_IO_PIN_INIT(DEVICE_ID_PCA9685, 10, SYS_IO_MODE_PWM), SYS_IO_PIN_INIT(DEVICE_ID_PCA9685, 11, SYS_IO_MODE_PWM)},
      .en_pins = {SYS_IO_PIN_INIT(DEVICE_ID_PCA9685, 12, SYS_IO_MODE_OUTPUT_PUSH_PULL), SYS_IO_PIN_INIT(DEVICE_ID_PCA9685, 13, SYS_IO_MODE_OUTPUT_PUSH_PULL),
                  SYS_IO_PIN_INIT(DEVICE_ID_PCA9685, 14, SYS_IO_MODE_OUTPUT_PUSH_PULL), SYS_IO_PIN_INIT(DEVICE_ID_PCA9685, 15, SYS_IO_MODE_OUTPUT_PUSH_PULL)},
      .current_adc_pins = {SYS_IO_PIN_NONE_INIT, SYS_IO_PIN_NONE_INIT, SYS_IO_PIN_NONE_INIT, SYS_IO_PIN_NONE_INIT},
      .nsleep_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 8, SYS_IO_MODE_OUTPUT_PUSH_PULL),
      .nfault_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 10, SYS_IO_MODE_INPUT),
      .vref_dac_pin = SYS_IO_PIN_INIT(DEVICE_ID_DAC53202, 1, SYS_IO_MODE_DAC),
      .pwm_freq_Hz = RUNIT_BOARD_DRV8962_PCA_PWM_HZ,
      .current_limit_mA = {RUNIT_BOARD_DRV8962_LIMIT_MA, RUNIT_BOARD_DRV8962_LIMIT_MA, RUNIT_BOARD_DRV8962_LIMIT_MA, RUNIT_BOARD_DRV8962_LIMIT_MA},
      .turn_off_at_ocp = true,
  }));
#endif

  ESP_LOGI(TAG, "onboard devices created");
  return NULL;
}
