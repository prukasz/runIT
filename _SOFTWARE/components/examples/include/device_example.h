#pragma once
#include "status.h"
#include "sys_io.h"

/**
 * One device create function responsible for adding new device to registry
 * Each param must be <= sizeof(void*) (4 bytes)
 * Shall be added all parameters responsible for device startup and not standardised in contracts
 * First argument must be uint8_t device_id
 */
status_rep_t d_example_create(uint8_t device_id, bool i2c_bus, uint8_t i2c_addr, uint8_t intr_io_device, sys_io_pin_num_t intr_io_num, sys_io_intr_config_t intr_config);