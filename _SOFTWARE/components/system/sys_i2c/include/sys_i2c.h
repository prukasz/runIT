
#pragma once
#include "sys_error.h"
#include "sys_i2c_types.h"

#define SYS_I2C_BUS0 0
#define SYS_I2C_BUS1 1

SE_MUST_USE err_h sys_i2c_init(i2c_master_bus_config_t* bus0_config, i2c_master_bus_config_t* bus1_config);
SE_MUST_USE err_h sys_i2c_add_driver(void* hw_handle);
SE_MUST_USE err_h sys_i2c_remove_driver(void* hw_handle);
SE_MUST_USE err_h sys_i2c_device_present(void* hw_handle);

/** Fills `dev` (no bus access). Call before sys_i2c_dev_add(). */
static inline void sys_i2c_dev_init(sys_i2c_dev_t* dev, bool bus1, uint8_t addr, uint32_t speed_hz) {
  *dev = (sys_i2c_dev_t){.speed_hz = speed_hz, .addr = addr, .bus1 = bus1};
}

/** Probes the chip (ERR_I2C_DEV_NOT_FOUND if silent) and attaches it to its bus. */
SE_MUST_USE err_h sys_i2c_dev_add(sys_i2c_dev_t* dev);
/** Detaches the chip. Safe on a device that was never added. */
SE_MUST_USE err_h sys_i2c_dev_remove(sys_i2c_dev_t* dev);

/* Raw transfers. Failures come back as ERR_ESP_ERR; the caller's dispatcher adds the device id. */
SE_MUST_USE err_h sys_i2c_write(sys_i2c_dev_t* dev, const uint8_t* write_buffer, size_t write_size);
SE_MUST_USE err_h sys_i2c_write_read(sys_i2c_dev_t* dev, const uint8_t* write_buffer, size_t write_size, uint8_t* read_buffer, size_t read_size);

/* Register helpers for the usual "register byte, then data" chips. */
#define SYS_I2C_REG_MAX_LEN 32
SE_MUST_USE err_h sys_i2c_reg_write(sys_i2c_dev_t* dev, uint8_t reg, const uint8_t* data, size_t len);
SE_MUST_USE err_h sys_i2c_reg_read(sys_i2c_dev_t* dev, uint8_t reg, uint8_t* data, size_t len);
/** Read-modify-write of one register: reg = (reg & ~mask) | (val & mask). */
SE_MUST_USE err_h sys_i2c_reg_update(sys_i2c_dev_t* dev, uint8_t reg, uint8_t mask, uint8_t val);

i2c_master_bus_handle_t sys_i2c_get_bus_handle(bool bus_num);
