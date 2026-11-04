#pragma once
/* Host stand-in for sys_io.h: the pin reference (components/sys/sys_io/sys_io.h). */

#include "sys_device.h"

typedef struct __attribute__((packed)) {
  uint16_t device;  /**< Device ID (SYS_DEVICE_ID_NONE if unused) */
  uint8_t pin;      /**< Pin index on that device */
} sys_io_pin_t;

#define SYS_IO_PIN_NONE_DEVICE SYS_DEVICE_ID_NONE
#define SYS_IO_PIN_NONE_INDEX  0xFFu
#define SYS_IO_PIN_NONE ((sys_io_pin_t){.device = SYS_IO_PIN_NONE_DEVICE, .pin = SYS_IO_PIN_NONE_INDEX})
