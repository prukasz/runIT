#include "runit_board_cfg.h"

#include <esp_log.h>
#include "devices.h"
#include "runit.h"
#include "runit_board_defs.h"
#include "sys_ble.h"
#include "sys_connector.h"
#include "sys_device.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power_manager.h"
#include "sys_uart.h"
#include "utils.h"

#define TAG __FILE_NAME__

/* Routing: logs and errors on BLE LOGS; telemetry and events on BLE TX; errors, telemetry and events on UART. */
err_h runit_board_connector_bindings_init(void) {
  ERR_TRY(sys_connector_bind(SYS_CONNECTOR_ERRORS, TRANSPORT_UART, SYS_UART_ENDPOINT));
  ERR_TRY(sys_connector_bind(SYS_CONNECTOR_TELEMETRY, TRANSPORT_UART, SYS_UART_ENDPOINT));
  ERR_TRY(sys_connector_bind(SYS_CONNECTOR_EVENTS, TRANSPORT_UART, SYS_UART_ENDPOINT));
  ERR_TRY(sys_connector_bind(SYS_CONNECTOR_LOGS, TRANSPORT_BLE, SYS_BLE_CHR_RUNIT_LOGS));
  ERR_TRY(sys_connector_bind(SYS_CONNECTOR_ERRORS, TRANSPORT_BLE, SYS_BLE_CHR_RUNIT_LOGS));
  ERR_TRY(sys_connector_bind(SYS_CONNECTOR_TELEMETRY, TRANSPORT_BLE, SYS_BLE_CHR_RUNIT_TX));
  ERR_TRY(sys_connector_bind(SYS_CONNECTOR_EVENTS, TRANSPORT_BLE, SYS_BLE_CHR_RUNIT_TX));
  return NULL;
}

/* Power layout of this board revision: both TPS55289 rails budgeted, the INA3221 measures them and the input. */
static const sys_power_consumer_t k_power_consumers[] = {
    {.vreg = DEVICE_ID_TPS55289_2,
     .monitor = {.device = DEVICE_ID_INA3221, .channel = RUNIT_BOARD_INA_CH_RAIL_A},
     .min_mA = TPS55289_MIN_CURRENT_MA},
    {.vreg = DEVICE_ID_TPS55289_1,
     .monitor = {.device = DEVICE_ID_INA3221, .channel = RUNIT_BOARD_INA_CH_RAIL_B},
     .min_mA = TPS55289_MIN_CURRENT_MA},
};

/* Default response per power event (rev 1); changed at runtime with SET_RESPONSE. */
static const uint8_t k_power_responses[SYS_POWER_KIND_COUNT] = {
    [SYS_POWER_KIND_OVP] = SYS_POWER_RESPONSE_DISABLE,
    [SYS_POWER_KIND_UVP] = SYS_POWER_RESPONSE_NOTIFY,
    [SYS_POWER_KIND_SPC] = SYS_POWER_RESPONSE_DISABLE,
    [SYS_POWER_KIND_OCP_WARNING] = SYS_POWER_RESPONSE_NOTIFY,
    [SYS_POWER_KIND_OCP_CRITICAL] = SYS_POWER_RESPONSE_NOTIFY,  // bring-up: a rail in current limit keeps running
    [SYS_POWER_KIND_OTP] = SYS_POWER_RESPONSE_SAFE_STATE,
    [SYS_POWER_KIND_BATTERY_LOW] = SYS_POWER_RESPONSE_NOTIFY,
    [SYS_POWER_KIND_BATTERY_CRITICAL] = SYS_POWER_RESPONSE_SAFE_STATE,
    [SYS_POWER_KIND_BUDGET_EXCEEDED] = SYS_POWER_RESPONSE_DISABLE,
    [SYS_POWER_KIND_SOURCE_CHANGED] = SYS_POWER_RESPONSE_NOTIFY,
};

/* The eFuse power-good outputs on the TCA6424A: VUSB_OK 3, VEXT_OK 4 (HARDWARE.md). They are inputs after the TCA6424A is created. */
static const sys_power_source_pins_t k_source_pins = {
    .usb_ok = {.device = DEVICE_ID_TCA6424A, .pin = RUNIT_BOARD_TCA_VUSB_OK},
    .ext_ok = {.device = DEVICE_ID_TCA6424A, .pin = RUNIT_BOARD_TCA_VEXT_OK},
};

static const sys_power_board_t k_power_board = {
    .max_mV = RUNIT_BOARD_POWER_LIMIT_MV,
    .max_mA = RUNIT_BOARD_POWER_LIMIT_MA,
    .reserve_mW = RUNIT_BOARD_POWER_RESERVE_MW,
    .unknown_source_mA = RUNIT_BOARD_POWER_UNKNOWN_SOURCE_MA,
    .vreg_efficiency_pct = RUNIT_BOARD_VREG_EFFICIENCY_PCT,
    .input = {.device = DEVICE_ID_INA3221, .channel = RUNIT_BOARD_INA_CH_INPUT},
    .usb_pd = DEVICE_ID_AP33772S,
    .source_pins = &k_source_pins,
    .consumers = k_power_consumers,
    .consumer_count = ARRAY_LEN(k_power_consumers),
    .responses = k_power_responses,
};

err_h runit_board_power_init(void) {
  return sys_power_manager_init(&k_power_board, runit_enter_safe_state);
}

/* ---- Onboard devices ---- */

#define BOARD_PIN(dev, p) ((sys_io_pin_t){.device = (dev), .pin = (p)})
#define CREATE_ARGS_MAX 96u  // the largest create struct (the DRV8962: 83 B) + the type

// CREATE through the registry, like a remote command: args = [type u16][cfg].
static err_h create_device(uint16_t id, uint16_t type, const void* cfg, uint32_t cfg_len) {
  uint8_t args[CREATE_ARGS_MAX];
  ERR_CHECK(sizeof(type) + cfg_len <= sizeof(args), ERR_DEV_BAD_ARG);
  memcpy(args, &type, sizeof(type));
  if (cfg_len > 0) {
    memcpy(args + sizeof(type), cfg, cfg_len);
  }
  return sys_device_call(id, SYS_DEVICE_OP_CREATE, args, (uint32_t)sizeof(type) + cfg_len, NULL, 0, NULL);
}

/* One chip of the board. A failure is logged and the rest are still created (a missing chip must not take the others
 * down); the first failure is returned to the boot sequence, the others are reported. */
static void create_chip(err_h* first, const char* name, uint16_t id, uint16_t type, const void* cfg, uint32_t cfg_len) {
  err_h err = create_device(id, type, cfg, cfg_len);
  if (err == NULL) {
    return;
  }
  ESP_LOGE(TAG, "onboard device %s (id %u) failed", name, (unsigned)id);
  if (*first == NULL) {
    *first = err;
  } else {
    err_report(err);
  }
}

#define CREATE_CHIP(first, name, id, type, cfg) create_chip(first, name, id, type, &(cfg), sizeof(cfg))

static void drv8962_pins_none(drv8962_cfg_t* cfg) {
  for (uint8_t i = 0; i < DRV8962_PINS; i++) {
    cfg->pin[i] = SYS_IO_PIN_NONE;
  }
}

err_h runit_board_devices_init(void) {
  // The base everything stands on: without the ESP pins and the expander nothing else can be created.
  ERR_TRY(create_device(DEVICE_ID_GPIO_ESP, SYS_DEVICE_TYPE_GPIO_ESP, NULL, 0));
  const tca6424a_cfg_t tca = {
      .i2c_bus = SYS_I2C_BUS_0,
      .i2c_addr = RUNIT_BOARD_TCA_ADDR,
      .intr_pin = BOARD_PIN(DEVICE_ID_GPIO_ESP, RUNIT_BOARD_TCA_INT_ESP_PIN),
      .rst_pin = BOARD_PIN(DEVICE_ID_GPIO_ESP, RUNIT_BOARD_TCA_RST_ESP_PIN),
  };
  ERR_TRY(create_device(DEVICE_ID_TCA6424A, SYS_DEVICE_TYPE_TCA6424A, &tca, sizeof(tca)));
  // The LEDs (push-pull outputs, high = on). Pin 22 must be an output too: left as an input it floats and toggles
  // at ~25 kHz, and every toggle is an INT edge (measured on the PCB 2026-10-11: the event queue overflowed).
  const sys_io_pin_t spare = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_LED_SPARE);
  ERR_TRY(sys_io_set_mode(spare, SYS_IO_MODE_OUTPUT_PUSH_PULL));
  const sys_io_pin_t led = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_LED_STATUS);
  ERR_TRY(sys_io_set_mode(led, SYS_IO_MODE_OUTPUT_PUSH_PULL));
  ERR_TRY(sys_io_set_level(led, true));

  // The other chips, in dependency order (a device can only use devices that exist).
  err_h first = NULL;
  const power_mux_cfg_t mux_drv1 = {.en_a_pin = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_DRV1_VM_VSUP),
                                    .en_b_pin = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_DRV1_VM_RAIL_B),
                                    .active_level = 1, .switch_delay_ms = 5};
  CREATE_CHIP(&first, "power mux DRV8962 #1", DEVICE_ID_SUPPLY_DRV8962_0, SYS_DEVICE_TYPE_POWER_MUX, mux_drv1);
  const power_mux_cfg_t mux_drv2 = {.en_a_pin = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_DRV2_VM_VSUP),
                                    .en_b_pin = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_DRV2_VM_RAIL_A),
                                    .active_level = 1, .switch_delay_ms = 5};
  CREATE_CHIP(&first, "power mux DRV8962 #2", DEVICE_ID_SUPPLY_DRV8962_1, SYS_DEVICE_TYPE_POWER_MUX, mux_drv2);

  const ads7128_cfg_t ads = {.i2c_bus = SYS_I2C_BUS_0, .i2c_addr = 0x10,
                             .intr_pin = BOARD_PIN(DEVICE_ID_GPIO_ESP, RUNIT_BOARD_ADS_ALERT_ESP_PIN),
                             .full_scale_mV = RUNIT_BOARD_ADS_FULL_SCALE_MV};
  CREATE_CHIP(&first, "ADS7128", DEVICE_ID_ADS7128, SYS_DEVICE_TYPE_ADS7128, ads);

  const pca9685_cfg_t pca = {.i2c_bus = SYS_I2C_BUS_0, .i2c_addr = 0x60,
                             .oe_pin = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_PCA_OE)};
  CREATE_CHIP(&first, "PCA9685", DEVICE_ID_PCA9685, SYS_DEVICE_TYPE_PCA9685, pca);

  const tps55289_cfg_t tps_a = {.i2c_bus = SYS_I2C_BUS_0, .i2c_addr = TPS55289_I2C_ADDR_74,
                                .intr_pin = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_TPS_A_INT),
                                .en_pin = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_TPS_A_EN)};
  CREATE_CHIP(&first, "TPS55289 rail A", DEVICE_ID_TPS55289_2, SYS_DEVICE_TYPE_TPS55289, tps_a);
  const tps55289_cfg_t tps_b = {.i2c_bus = SYS_I2C_BUS_0, .i2c_addr = TPS55289_I2C_ADDR_75,
                                .intr_pin = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_TPS_B_INT),
                                .en_pin = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_TPS_B_EN)};
  CREATE_CHIP(&first, "TPS55289 rail B", DEVICE_ID_TPS55289_1, SYS_DEVICE_TYPE_TPS55289, tps_b);

  // All three shunts are wired reversed (the input and both rails). Critical on TCA 6, warning on TCA 5: measured on
  // the PCB 2026-09-27.
  const ina3221_cfg_t ina = {.i2c_bus = SYS_I2C_BUS_0, .i2c_addr = INA3221_I2C_ADDR_MIN,
                             .crit_pin = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_INA_CRIT),
                             .warn_pin = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_INA_WARN),
                             .inverted_mask = 0x07, .shunt_mohm = RUNIT_BOARD_INA_SHUNT_MOHM};
  CREATE_CHIP(&first, "INA3221", DEVICE_ID_INA3221, SYS_DEVICE_TYPE_INA3221, ina);

  const ap33772s_cfg_t ap = {.i2c_bus = SYS_I2C_BUS_0, .i2c_addr = AP33772S_I2C_ADDR,
                             .intr_pin = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_AP_INT)};
  CREATE_CHIP(&first, "AP33772S", DEVICE_ID_AP33772S, SYS_DEVICE_TYPE_AP33772S, ap);

  // A0 to AGND (0x48, probed on the PCB 2026-09-24). OUT0 / OUT1 = VREF of DRV8962 #1 / #2.
  const dac53202_cfg_t dac = {.i2c_bus = SYS_I2C_BUS_0, .i2c_addr = 0x48};
  CREATE_CHIP(&first, "DAC53202", DEVICE_ID_DAC53202, SYS_DEVICE_TYPE_DAC53202, dac);

  // DRV8962 #1: IN / EN on the ESP, IPROPI on the ESP ADC, nSLEEP / nFAULT on the TCA6424A, VREF = DAC OUT0.
  drv8962_cfg_t drv1 = {.topology = DRV8962_TOPOLOGY_2_FULL_BRIDGES, .turn_off_at_ocp = 1};
  drv8962_pins_none(&drv1);
  static const uint8_t k_drv1_in[4] = {21, 47, 48, 45}, k_drv1_en[4] = {38, 39, 2, 1}, k_drv1_ipropi[4] = {7, 6, 5, 4};
  for (uint8_t i = 0; i < 4; i++) {
    drv1.pin[DRV8962_PIN_IN1 + i] = BOARD_PIN(DEVICE_ID_GPIO_ESP, k_drv1_in[i]);
    drv1.pin[DRV8962_PIN_EN1 + i] = BOARD_PIN(DEVICE_ID_GPIO_ESP, k_drv1_en[i]);
    drv1.pin[DRV8962_PIN_IPROPI1 + i] = BOARD_PIN(DEVICE_ID_GPIO_ESP, k_drv1_ipropi[i]);
    drv1.current_limit_mA[i] = RUNIT_BOARD_DRV8962_LIMIT_MA;
  }
  drv1.pin[DRV8962_PIN_NSLEEP] = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_DRV1_NSLEEP);
  drv1.pin[DRV8962_PIN_NFAULT] = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_DRV1_NFAULT);
  drv1.pin[DRV8962_PIN_VREF] = BOARD_PIN(DEVICE_ID_DAC53202, 0);
  CREATE_CHIP(&first, "DRV8962 #1", DEVICE_ID_DRV8962_0, SYS_DEVICE_TYPE_DRV8962, drv1);

  // DRV8962 #2: IN1-4 = PCA9685 CH 8..11, EN1-4 = CH 12..15. IPROPI has resistors only (no ADC); VREF = DAC OUT1.
  drv8962_cfg_t drv2 = {.topology = DRV8962_TOPOLOGY_2_FULL_BRIDGES, .turn_off_at_ocp = 1,
                        .pwm_freq_Hz = RUNIT_BOARD_DRV8962_PCA_PWM_HZ};
  drv8962_pins_none(&drv2);
  for (uint8_t i = 0; i < 4; i++) {
    drv2.pin[DRV8962_PIN_IN1 + i] = BOARD_PIN(DEVICE_ID_PCA9685, 8 + i);
    drv2.pin[DRV8962_PIN_EN1 + i] = BOARD_PIN(DEVICE_ID_PCA9685, 12 + i);
    drv2.current_limit_mA[i] = RUNIT_BOARD_DRV8962_LIMIT_MA;
  }
  drv2.pin[DRV8962_PIN_NSLEEP] = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_DRV2_NSLEEP);
  drv2.pin[DRV8962_PIN_NFAULT] = BOARD_PIN(DEVICE_ID_TCA6424A, RUNIT_BOARD_TCA_DRV2_NFAULT);
  drv2.pin[DRV8962_PIN_VREF] = BOARD_PIN(DEVICE_ID_DAC53202, 1);
  CREATE_CHIP(&first, "DRV8962 #2", DEVICE_ID_DRV8962_1, SYS_DEVICE_TYPE_DRV8962, drv2);

  return first;
}
