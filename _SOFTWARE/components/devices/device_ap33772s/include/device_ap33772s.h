#pragma once
#include "sys_error.h"
#include "sys_io.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Configuration for AP33772S USB PD controller.
 */
typedef struct d_ap33772s_cfg_t {
  uint8_t device_id; /**< MUST be first member */
  bool i2c_bus;
  uint8_t i2c_addr;
  sys_io_pin_ref_t intr_pin;
} d_ap33772s_cfg_t;

/**
 * @brief Initialize and register the AP33772S USB PD controller.
 *
 * @param cfg Pointer to device configuration struct
 * @return err_h Status of the registration
 */
err_h d_ap33772s_create(const d_ap33772s_cfg_t* cfg);

