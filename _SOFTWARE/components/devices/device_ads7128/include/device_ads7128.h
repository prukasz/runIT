#pragma once
#include "status.h"
#include "sys_io.h"

status_rep_t d_ads7128_create(uint8_t device_id,
                              bool i2c_bus,
                              uint8_t i2c_addr,
                              uint8_t intr_io_device,
                              sys_io_pin_num_t intr_io_num,
                              sys_io_mode_e intr_io_mode,
                              uint32_t vref_mv);
