#pragma once

#include "sys_error.h"

err_h runit_board_i2c_init(void);
err_h runit_board_power_init(void);
err_h runit_board_ble_init(void);
err_h runit_board_connector_bindings_init(void);
err_h runit_board_devices_init(void);
err_h runit_board_bind_boot_action(void);
err_h runit_board_invoke_boot_action(void);
