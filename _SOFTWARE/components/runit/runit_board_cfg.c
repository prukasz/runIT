#include "runit_board_cfg.h"

#include <esp_log.h>
#include <sdkconfig.h>
#include "devices.h"
#include "runit_board_defs.h"
#include "sys_ble.h"
#include "sys_data_connector_ble.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power.h"
#include "sys_actions.h"
#include "sys_actions_static.h"

#define TAG "board_configuration"
#define OWNER OWNER_SYS_DEVICE_BASE

err_h runit_board_bind_boot_action(void) {
#if RUNIT_SKIP_DEVICE_INIT
  return NULL;
#else
  return sys_actions_bind_static(CONFIG_SYS_ACTION_ID_BOOT, runit_board_devices_init);
#endif
}

err_h runit_board_invoke_boot_action(void) {
#if RUNIT_SKIP_DEVICE_INIT
  return NULL;
#else
  return sys_actions_invoke(SYS_ACTION_SCOPE_STATIC, CONFIG_SYS_ACTION_ID_BOOT);
#endif
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

err_h runit_board_power_init(void) {
  SE_RET_IF_ERR(sys_power_set_limits(RUNIT_BOARD_POWER_LIMIT_MV, RUNIT_BOARD_POWER_LIMIT_MA, RUNIT_BOARD_POWER_BUDGET_MW));
  ESP_LOGI(TAG, "power limits configured");
  return NULL;
}

err_h runit_board_ble_init(void) {
  sys_ble_svc_cfg_t service_cfg = {.uuid = SYS_BLE_SVC_RUNIT, .is_primary = true};
  SE_RET_IF_ERR(sys_ble_service_create(&service_cfg));

  sys_ble_char_cfg_t rx_cfg = {.uuid = SYS_BLE_CHR_RUNIT_RX, .is_write = true, .desc = "runit RX", .rx_buffer_size = 512};
  SE_RET_IF_ERR(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &rx_cfg));
  sys_ble_char_cfg_t tx_cfg = {.uuid = SYS_BLE_CHR_RUNIT_TX, .is_notify = true, .desc = "runit TX", .tx_buffer_size = 1024};
  SE_RET_IF_ERR(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &tx_cfg));
  sys_ble_char_cfg_t status_cfg = {.uuid = SYS_BLE_CHT_RUNIT_STATUS, .is_notify = true, .desc = "runit Status", .tx_buffer_size = 512};
  SE_RET_IF_ERR(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &status_cfg));
  sys_ble_char_cfg_t logs_cfg = {.uuid = SYS_BLE_CHR_RUNIT_LOGS, .is_notify = true, .desc = "runit LOGS", .tx_buffer_size = 2048};
  SE_RET_IF_ERR(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &logs_cfg));

  SE_RET_IF_ERR(sys_ble_database_sync());
  ESP_LOGI(TAG, "BLE initialized");
  return NULL;
}

err_h runit_board_connector_bindings_init(void) {
  SE_RET_IF_ERR(sys_data_connector_register_ble_provider());
  SE_RET_IF_ERR(sys_data_connector_bind_tx(sys_data_connector_get(CONFIG_SYS_DATA_CONN_ID_LOGS), SYS_DATA_PROVIDER_BLE, SYS_DATA_BLE_ARG(SYS_BLE_CHR_RUNIT_LOGS)));
  SE_RET_IF_ERR(sys_data_connector_bind_tx(sys_data_connector_get(CONFIG_SYS_DATA_CONN_ID_ERRORS), SYS_DATA_PROVIDER_BLE, SYS_DATA_BLE_ARG(SYS_BLE_CHR_RUNIT_LOGS)));
  SE_RET_IF_ERR(sys_data_connector_bind_tx(sys_data_connector_get(CONFIG_SYS_DATA_CONN_ID_TELEMETRY), SYS_DATA_PROVIDER_BLE, SYS_DATA_BLE_ARG(SYS_BLE_CHR_RUNIT_TX)));
  SE_RET_IF_ERR(sys_data_connector_bind_tx(sys_data_connector_get(CONFIG_SYS_DATA_CONN_ID_INTERFACE), SYS_DATA_PROVIDER_BLE, SYS_DATA_BLE_ARG(SYS_BLE_CHR_RUNIT_TX)));
  SE_RET_IF_ERR(sys_data_connector_bind_rx(sys_data_connector_get(CONFIG_SYS_DATA_CONN_ID_INTERFACE), SYS_DATA_PROVIDER_BLE, SYS_DATA_BLE_ARG(SYS_BLE_CHR_RUNIT_RX)));
  ESP_LOGI(TAG, "BLE data connectors bound");
  return NULL;
}

err_h runit_board_devices_init(void) {
  SE_RET_IF_ERR(d_gpio_esp_create(&(d_gpio_esp_cfg_t){.device_id = DEVICE_ID_GPIO_ESP}));
  SE_RET_IF_ERR(d_tca6424a_create(&(d_tca6424a_cfg_t){
      .device_id = DEVICE_ID_TCA6424A, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x23,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 9, SYS_IO_MODE_INPUT),
      .rst_pin = SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 8, SYS_IO_MODE_OUTPUT_PUSH_PULL),
  }));
  SE_RET_IF_ERR(d_ads7128_create(&(d_ads7128_cfg_t){
      .device_id = DEVICE_ID_ADS7128, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x10,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 42, SYS_IO_MODE_INPUT_PULLUP), .vref_mv = 20000,
  }));
  SE_RET_IF_ERR(d_pca9685_create(&(d_pca9685_cfg_t){
      .device_id = DEVICE_ID_PCA9685, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x60,
      .oe_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 0, SYS_IO_MODE_OUTPUT_PUSH_PULL),
  }));
  SE_RET_IF_ERR(d_tps55289_create(&(d_tps55289_cfg_t){
      .device_id = DEVICE_ID_TPS55289_0, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x74,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 1, SYS_IO_MODE_INPUT),
      .en_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 17, SYS_IO_MODE_OUTPUT_PUSH_PULL),
  }));
  SE_RET_IF_ERR(d_tps55289_create(&(d_tps55289_cfg_t){
      .device_id = DEVICE_ID_TPS55289_1, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x75,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 2, SYS_IO_MODE_INPUT),
      .en_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 16, SYS_IO_MODE_OUTPUT_PUSH_PULL),
  }));
  SE_RET_IF_ERR(d_ina3221_create(&(d_ina3221_cfg_t){
      .device_id = DEVICE_ID_INA3221, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x40,
      .crit_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 5, SYS_IO_MODE_INPUT),
      .warn_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 6, SYS_IO_MODE_INPUT),
  }));
  SE_RET_IF_ERR(d_ap33772s_create(&(d_ap33772s_cfg_t){
      .device_id = DEVICE_ID_AP33772S, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x52,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 12, SYS_IO_MODE_INPUT),
  }));
  SE_RET_IF_ERR(sys_io_set_mode(DEVICE_ID_TCA6424A, 22, SYS_IO_MODE_OUTPUT_PUSH_PULL));
  SE_RET_IF_ERR(sys_io_set_mode(DEVICE_ID_TCA6424A, 23, SYS_IO_MODE_OUTPUT_PUSH_PULL));

  ESP_LOGI(TAG, "onboard devices created");
  return NULL;
}
