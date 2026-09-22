#pragma once

#include "sys_error.h"

SE_MUST_USE err_h runit_board_i2c_init(void);
SE_MUST_USE err_h runit_board_power_init(void);
SE_MUST_USE err_h runit_board_ble_init(void);
SE_MUST_USE err_h runit_board_connector_bindings_init(void);
SE_MUST_USE err_h runit_board_error_sink_init(void);
SE_MUST_USE err_h runit_board_devices_init(void);
SE_MUST_USE err_h runit_board_bind_boot_action(void);
SE_MUST_USE err_h runit_board_invoke_boot_action(void);
