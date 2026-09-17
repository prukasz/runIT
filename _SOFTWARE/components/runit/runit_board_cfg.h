#pragma once
/******************************************************
Board specific hardware configurations
!Board rev. 1.0

!Note

Pin configs and device id / adresses shall not be changed
****************************************************** */

#include "runit_board_defs.h"
#include "sys_ble.h"
#include "sys_data_connector_ble.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_interface.h"
#include "sys_power.h"

/* ---------------------------------------------------------------------------
   Bring-up switches. Both default off; flip for bench testing, not for a
   shipping build.

   RUNIT_SKIP_DEVICE_INIT skips binding runit_at_boot to action 1, so no I2C
   device is probed or installed. Action 0 still gets a no-op static function
   bound in its place -- leaving it unbound would make sys_actions_init()'s
   unconditional invoke(0) report ERR_ACTION_NOT_FOUND at every boot. Useful
   when testing logic that doesn't need hardware, or on a board where a
   missing//faulty peripheral would otherwise stall boot.

   RUNIT_ENABLE_VM_SELFTEST runs vm_selftest_run() at the end of boot -- see
   [[vm_selftest.h]].

   RUNIT_ENABLE_VM_BENCH runs vm_bench_run() -- accessor resolve timings, see
   [[vm_bench.h]]. Safe to run alongside the self test: the test leaves the
   object table detached, and the benchmark's setup() rebuilds it before use.
   Worth running both after any change to the object layout or to the resolve
   path, so a timing win cannot quietly be a correctness loss.
   --------------------------------------------------------------------------- */
#ifndef RUNIT_DEV_PROFILE
#define RUNIT_DEV_PROFILE 1  /* 0 for production defaults, 1 for development */
#endif

#ifndef RUNIT_SKIP_DEVICE_INIT
  #if defined(CONFIG_RUNIT_SKIP_DEVICE_INIT)
    #define RUNIT_SKIP_DEVICE_INIT CONFIG_RUNIT_SKIP_DEVICE_INIT
  #elif RUNIT_DEV_PROFILE
    #define RUNIT_SKIP_DEVICE_INIT 1
  #else
    #define RUNIT_SKIP_DEVICE_INIT 0
  #endif
#endif

#ifndef RUNIT_ENABLE_VM_SELFTEST
  #if defined(CONFIG_RUNIT_ENABLE_VM_SELFTEST)
    #define RUNIT_ENABLE_VM_SELFTEST CONFIG_RUNIT_ENABLE_VM_SELFTEST
  #elif RUNIT_DEV_PROFILE
    #define RUNIT_ENABLE_VM_SELFTEST 1
  #else
    #define RUNIT_ENABLE_VM_SELFTEST 0
  #endif
#endif

#ifndef RUNIT_ENABLE_VM_BENCH
  #if defined(CONFIG_RUNIT_ENABLE_VM_BENCH)
    #define RUNIT_ENABLE_VM_BENCH CONFIG_RUNIT_ENABLE_VM_BENCH
  #else
    #define RUNIT_ENABLE_VM_BENCH 0
  #endif
#endif

/* Granular test section toggles (when RUNIT_ENABLE_VM_SELFTEST is 1) */
#define RUNIT_TEST_SECTION_OBJ 1     // Group 1: Object model, accessors, contracts (A-K, OBJ)
#define RUNIT_TEST_SECTION_LOADER 1  // Group 2: Loader & wire protocol (L, N, M, O, P)
#define RUNIT_TEST_SECTION_EXEC 1    // Group 3: Execution, blocks, math (R, BLK, S-Y, PIPE, PI, PRIME, OVERRIDE)
#define RUNIT_TEST_SECTION_SUB 1     // Group 4: Subscriptions & telemetry (SUB)

#define RUNIT_BOARD_POWER_LIMIT_MV 21000
#define RUNIT_BOARD_POWER_LIMIT_MA 5500
#define RUNIT_BOARD_POWER_BUDGET_MW (RUNIT_BOARD_POWER_LIMIT_MV * RUNIT_BOARD_POWER_LIMIT_MA / 1000)

static inline err_h sys_start_i2c(void) {
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

#define RUNIT_CHECK_ERR(call) \
  do {                        \
    err_h __rc_err = (call);  \
    if (__rc_err != NULL) {   \
      return __rc_err;        \
    }                         \
  } while (0)

/*System-level power budget config (board mounted) - device creation lives in runit_board_devices.h*/
static inline err_h sys_power_static_config(void) {
  RUNIT_CHECK_ERR(sys_power_set_limits(RUNIT_BOARD_POWER_LIMIT_MV, RUNIT_BOARD_POWER_LIMIT_MA, RUNIT_BOARD_POWER_BUDGET_MW));
  ESP_LOGI("static_config", "power limits configured");
  return NULL;
}

static inline err_h sys_ble_static_config(void) {
  sys_ble_svc_cfg_t runit_svc_cfg = {.uuid = SYS_BLE_SVC_RUNIT, .is_primary = true};
  RUNIT_CHECK_ERR(sys_ble_service_create(&runit_svc_cfg));

  sys_ble_char_cfg_t runit_chr_cfg_rx = {.uuid = SYS_BLE_CHR_RUNIT_RX, .is_write = true, .desc = "runit RX", .rx_buffer_size = 512};
  RUNIT_CHECK_ERR(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &runit_chr_cfg_rx));

  sys_ble_char_cfg_t runit_chr_cfg_tx = {.uuid = SYS_BLE_CHR_RUNIT_TX, .is_notify = true, .desc = "runit TX", .tx_buffer_size = 1024};
  RUNIT_CHECK_ERR(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &runit_chr_cfg_tx));

  sys_ble_char_cfg_t runit_chr_cfg_status = {.uuid = SYS_BLE_CHT_RUNIT_STATUS, .is_notify = true, .desc = "runit Status", .tx_buffer_size = 512};
  RUNIT_CHECK_ERR(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &runit_chr_cfg_status));

  sys_ble_char_cfg_t runit_chr_cfg_logs = {.uuid = SYS_BLE_CHR_RUNIT_LOGS, .is_notify = true, .desc = "runit LOGS", .tx_buffer_size = 2048};
  RUNIT_CHECK_ERR(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &runit_chr_cfg_logs));

  RUNIT_CHECK_ERR(sys_ble_database_sync());
  ESP_LOGI("static_config", "BLE initialized");
  return NULL;
}
/* Board-owned connector topology; the BLE adapter only supplies transport. */
#pragma push_macro("OWNER")
#undef OWNER
#define OWNER OWNER_SYS_INTERFACE_CLASS
static inline err_h runit_bind_ble_channel(uint8_t id, const char* name, uint8_t header, uint16_t tx_uuid, uint16_t rx_uuid) {
  if (!tx_uuid && !rx_uuid) return NULL;
  sys_data_connector_t* conn = sys_data_connector_create(id, name, header);
  SE_CHECK_IF_ALLOCATED(conn);
  if (rx_uuid) {
    RUNIT_CHECK_ERR(sys_data_connector_bind_rx(conn, SYS_DATA_PROVIDER_BLE, SYS_DATA_BLE_ARG(rx_uuid)));
  }
  if (tx_uuid) {
    RUNIT_CHECK_ERR(sys_data_connector_bind_tx(conn, SYS_DATA_PROVIDER_BLE, SYS_DATA_BLE_ARG(tx_uuid)));
  }
  return NULL;
}
#pragma pop_macro("OWNER")

static inline err_h runit_data_connector_static_config(void) {
  RUNIT_CHECK_ERR(sys_data_connector_register_ble_provider());
  RUNIT_CHECK_ERR(runit_bind_ble_channel(CONN_ID_LOGS, "logs", PACKET_HEADER_LOGS, SYS_BLE_CHR_RUNIT_LOGS, 0));
  RUNIT_CHECK_ERR(runit_bind_ble_channel(CONN_ID_ERRORS, "errors", PACKET_HEADER_ERRORS, SYS_BLE_CHR_RUNIT_LOGS, 0));
  RUNIT_CHECK_ERR(runit_bind_ble_channel(CONN_ID_TELEMETRY, "telemetry", PACKET_HEADER_TX, SYS_BLE_CHR_RUNIT_TX, 0));
  RUNIT_CHECK_ERR(runit_bind_ble_channel(CONN_ID_INTERFACE, "interface", PACKET_HEADER_TX, SYS_BLE_CHR_RUNIT_TX, SYS_BLE_CHR_RUNIT_RX));
  return NULL;
}
#undef RUNIT_CHECK_ERR
