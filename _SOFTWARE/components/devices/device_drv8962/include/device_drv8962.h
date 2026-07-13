#pragma once
#include "status.h"
#include "sys_device.h"
#include "sys_power.h"
#include "sys_io.h"

typedef struct {
  uint8_t in_devices[4];
  sys_io_pin_num_t in_pins[4];

  uint8_t en_devices[4];
  sys_io_pin_num_t en_pins[4];

  uint8_t nsleep_device;
  sys_io_pin_num_t nsleep_pin;

  uint8_t ipropi_devices[4];
  sys_io_pin_num_t ipropi_pins[4];

  uint8_t nfault_device;
  sys_io_pin_num_t nfault_pin;

  uint8_t mode_device;
  sys_io_pin_num_t mode_pin;
  bool mode_val; // true: 70ns rise/fall time, false: 140ns

  uint8_t ocpm_device;
  sys_io_pin_num_t ocpm_pin;
  bool ocpm_val; // true: auto-retry, false: latch-off

  uint32_t r_ipropi_ohms[4]; // Resistors connected to IPROPI pins (e.g. 3090)
} drv8962_config_t;

status_rep_t d_drv8962_create(uint8_t device_id, const drv8962_config_t* config);
