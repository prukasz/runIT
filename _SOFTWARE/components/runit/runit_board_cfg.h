#pragma once
/******************************************************
Board specific hardware configurations
!Board rev. 1.0

!Note

Pin configs and device id / adresses shall not be changed
****************************************************** */

#include "device_ap33772s.h"
#include "device_dac53202.h"
#include "device_gpio_esp.h"
#include "device_ina3221.h"
#include "device_pca9685.h"
#include "device_tca6424a.h"
#include "device_tps55289.h"
#include "runit_board_defs.h"
#include "status.h"
#include "sys_ble.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power.h"

#define RUNIT_BOARD_POWER_LIMIT_MV 21000
#define RUNIT_BOARD_POWER_LIMIT_MA 5500
#define RUNIT_BOARD_POWER_BUDGET_MW (RUNIT_BOARD_POWER_LIMIT_MV * RUNIT_BOARD_POWER_LIMIT_MA / 1000)

status_rep_t sys_start_i2c(void) {
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
      .scl_io_num = SYS_PIN_I2C_2_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = SYS_I2C_PULLUP_ENABLE,
  };
  return sys_i2c_init(&bus0_cfg, &bus1_cfg);
}
/*Static definition of io devices (board mounted)*/
status_rep_t sys_io_static_config(void) {
  STA_R_ON_ERR(d_gpio_esp_create(DEVICE_ID_GPIO_ESP));
  // STA_R_ON_ERR(d_tca6424a_create(DEVICE_ID_TCA6424A, SYS_I2C_BUS_INTERNAL, 0x23, DEVICE_ID_GPIO_ESP, 9, SYS_IO_MODE_INPUT, DEVICE_ID_GPIO_ESP, 8, SYS_IO_MODE_OUTPUT_PUSH_PULL));
  STA_R_ON_ERR(d_pca9685_create(DEVICE_ID_PCA9685, SYS_I2C_BUS_INTERNAL, 0x60, DEVICE_ID_TCA6424A, 0xFF, SYS_IO_MODE_OUTPUT_PUSH_PULL));
  // STA_R_ON_ERR(d_dac53202_create(DEVICE_ID_DAC53202, SYS_I2C_BUS_INTERNAL, 0x13));
  ESP_LOGI("static_config", "io initialized");
  return STA_OK;
}
/*Static definition of power devices (board mounted)*/
status_rep_t sys_power_static_config(void) {
  STA_R_ON_ERR(sys_power_set_limits(RUNIT_BOARD_POWER_LIMIT_MV, RUNIT_BOARD_POWER_LIMIT_MA, RUNIT_BOARD_POWER_BUDGET_MW));
  // STA_R_ON_ERR(d_tps55289_create(DEVICE_ID_TPS55289_0, SYS_I2C_BUS_INTERNAL, 0x74, DEVICE_ID_TCA6424A, 1, SYS_IO_MODE_INPUT, DEVICE_ID_TCA6424A, 17, SYS_IO_MODE_OUTPUT_PUSH_PULL));
  // STA_R_ON_ERR(d_tps55289_create(DEVICE_ID_TPS55289_1, SYS_I2C_BUS_INTERNAL, 0x75, DEVICE_ID_TCA6424A, 2, SYS_IO_MODE_INPUT, DEVICE_ID_TCA6424A, 16, SYS_IO_MODE_OUTPUT_PUSH_PULL));
  // STA_R_ON_ERR(d_ina3221_create(DEVICE_ID_INA3221, SYS_I2C_BUS_INTERNAL, 0x60, DEVICE_ID_TCA6424A, 5, SYS_IO_MODE_INPUT, DEVICE_ID_TCA6424A, 6, SYS_IO_MODE_INPUT));
  // STA_R_ON_ERR(d_ap33772s_create(DEVICE_ID_AP33772S, SYS_I2C_BUS_INTERNAL, 0x14, DEVICE_ID_TCA6424A, 12, SYS_IO_MODE_INPUT));
  ESP_LOGI("static_config", "power initialized");
  return STA_OK;
}

status_rep_t sys_ble_static_config() {
  STA_R_ON_ERR(sys_ble_init());

  sys_ble_svc_cfg_t runit_svc_cfg = {.uuid = SYS_BLE_SVC_RUNIT, .is_primary = true};
  STA_R_ON_ERR(sys_ble_service_create(&runit_svc_cfg));

  sys_ble_char_create_t runit_chr_cfg_rx = {.info = {.uuid = SYS_BLE_CHR_RUNIT_RX, .is_write = true, .desc = "runit RX"}, .rx_buffer_size = 512};
  STA_R_ON_ERR(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &runit_chr_cfg_rx));

  sys_ble_char_create_t runit_chr_cfg_tx = {.info = {.uuid = SYS_BLE_CHR_RUNIT_TX, .is_notify = true, .desc = "runit TX"}, .rx_buffer_size = 0};
  STA_R_ON_ERR(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &runit_chr_cfg_tx));

  sys_ble_tx_buf_cfg_t runit_buff_cfg_tx = {.buffer_id = 0, .buff_type = RINGBUF_TYPE_NOSPLIT, .size = 1024, .auto_pack = false, .header = 0, .priority = 1, .is_indication = false};
  STA_R_ON_ERR(sys_ble_char_assign_tx_buffer(SYS_BLE_CHR_RUNIT_TX, &runit_buff_cfg_tx));

  sys_ble_char_create_t runit_chr_cfg_status = {.info = {.uuid = SYS_BLE_CHT_RUNIT_STATUS, .is_notify = true, .desc = "runit Status"}, .rx_buffer_size = 0};
  STA_R_ON_ERR(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &runit_chr_cfg_status));

  sys_ble_tx_buf_cfg_t runit_buff_cfg_status = {.buffer_id = 0, .buff_type = RINGBUF_TYPE_BYTE_BUF, .size = 512, .auto_pack = true, .header = PACKET_HEADER_STATUS, .item_size = sizeof(status_rep_t), .priority = 1, .is_indication = false};
  STA_R_ON_ERR(sys_ble_char_assign_tx_buffer(SYS_BLE_CHT_RUNIT_STATUS, &runit_buff_cfg_status));

  sys_ble_char_create_t runit_chr_cfg_logs = {.info = {.uuid = SYS_BLE_CHR_RUNIT_LOGS, .is_notify = true, .desc = "runit LOGS"}, .rx_buffer_size = 0};
  STA_R_ON_ERR(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &runit_chr_cfg_logs));

  sys_ble_tx_buf_cfg_t runit_buff_cfg_logs = {.buffer_id = 0, .buff_type = RINGBUF_TYPE_NOSPLIT, .size = 2048, .auto_pack = false, .header = 0, .priority = 2, .is_indication = false};
  STA_R_ON_ERR(sys_ble_char_assign_tx_buffer(SYS_BLE_CHR_RUNIT_LOGS, &runit_buff_cfg_logs));

  STA_R_ON_ERR(sys_ble_database_sync());
  ESP_LOGI("static_config", "BLE initialized");
  return STA_OK;
}
