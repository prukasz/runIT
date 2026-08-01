#include "runit.h"
#include <esp_log.h>
#include <string.h>
#include "runit_board_cfg.h"
#include "runit_board_defs.h"
#include "runit_board_devices.h"
#include "sys_actions.h"
#include "sys_interface.h"

static const char* TAG = "runit_app";

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

void runit_start(void) {
  SE_init();
  SE_ORIGIN_CALL(sys_start_i2c());
  SE_ORIGIN_CALL(sys_power_static_config());
  SE_ORIGIN_CALL(sys_ble_static_config());
  SE_ORIGIN_CALL(SE_configure(&s_runit_error_cfg));
  SE_ORIGIN_CALL(sys_interface_init());
  // Must come before sys_actions_init(): the boot action (id 0) needs its
  // static function bound before init's unconditional invoke(0) runs.
  SE_ORIGIN_CALL(sys_actions_bind_static(0, runit_at_boot, NULL));
  // Must come before sys_interface_bind_ble_rx(): class registration and the
  // recording tap are boot-only, not safe against a running RX pump.
  SE_ORIGIN_CALL(sys_actions_init());
  SE_ORIGIN_CALL(sys_interface_bind_ble_rx(SYS_BLE_CHR_RUNIT_RX, RUNIT_BLE_RX_FRAME_MAX));
  runit_test_pca9685_start();
  ESP_LOGI(TAG, "runIT boot sequence complete");
}
