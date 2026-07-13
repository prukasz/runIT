
#pragma once
#include "status.h"
#include "sys_i2c_types.h"


#define SYS_I2C_BUS0 0
#define SYS_I2C_BUS1 1

status_rep_t sys_i2c_init(i2c_master_bus_config_t* bus0_config, i2c_master_bus_config_t* bus1_config);
status_rep_t sys_i2c_add_driver(void* device_handle);
status_rep_t sys_i2c_remove_driver(void* device_handle);
status_rep_t sys_i2c_device_present(void* device_handle);

i2c_master_bus_handle_t sys_i2c_get_bus_handle(bool bus_num);
