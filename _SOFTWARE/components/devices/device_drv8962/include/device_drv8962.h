#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "sys_error.h"
#include "sys_io.h"
#include "sys_hbridge.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  DRV8962_TOPOLOGY_2_FULL_BRIDGES = 0, /* Ch 0 = OUT1+2 (Motor A), Ch 1 = OUT3+4 (Motor B) */
  DRV8962_TOPOLOGY_4_HALF_BRIDGES,     /* Ch 0..3 = OUT1..4 (Individual half bridges / solenoids) */
} drv8962_topology_e;

typedef struct d_drv8962_cfg_t {
  uint8_t device_id;                   /* MUST be first - SYS_DEVICE_CREATE reads it */
  drv8962_topology_e topology;         /* Full bridge pairs vs individual half bridges */

  sys_io_pin_ref_t in_pins[4];         /* IN1, IN2, IN3, IN4 */
  sys_io_pin_ref_t en_pins[4];         /* EN1, EN2, EN3, EN4 (or SYS_IO_PIN_NONE) */
  sys_io_pin_ref_t nsleep_pin;         /* Shared nSLEEP pin (wake / reset) */
  sys_io_pin_ref_t nfault_pin;         /* Shared nFAULT pin (falling edge interrupt) */
  sys_io_pin_ref_t current_adc_pins[4];/* IPROPI1, IPROPI2, IPROPI3, IPROPI4 (or SYS_IO_PIN_NONE) */
  sys_io_pin_ref_t vref_dac_pin;       /* VREF chopping DAC pin (or SYS_IO_PIN_NONE) */

  uint32_t pwm_freq_Hz;                /* Default 20000 (20 kHz) */
  uint32_t ripropi_ohms[4];            /* Resistor on IPROPI pins (e.g. 3090 Ohm) */
  uint32_t current_limit_mA[4];        /* Safety limit in mA (0 to disable) */
  bool turn_off_at_ocp;                /* When true, automatically cuts drive on overcurrent */
} d_drv8962_cfg_t;

SE_MUST_USE err_h d_drv8962_create(const d_drv8962_cfg_t* cfg);

#ifdef __cplusplus
}
#endif

