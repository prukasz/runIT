#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/i2c_master.h"


/**
 * @brief Set the given pin to HIGH or LOW, generating interrupt accordingly.
 */
void tca_mock_set_pin_level(uint8_t pin, bool level);

/**
 * @brief Mock I2C transmit function.
 */
esp_err_t tca_transmit(i2c_master_dev_handle_t handle, const uint8_t *write_buffer, size_t write_buffer_len, int xfer_timeout_ms);

/**
 * @brief Mock I2C transmit_receive function.
 */
esp_err_t tca_transmit_receive(i2c_master_dev_handle_t handle, const uint8_t *write_buffer, size_t write_buffer_len, uint8_t *read_buffer, size_t read_buffer_len, int xfer_timeout_ms);

/**
 * @brief Get the simulated level of the INT pin (0 = active/low, 1 = inactive/high).
 */
int tca_get_int_pin_level(void);

/**
 * @brief Register a callback to be invoked when the INT pin becomes active.
 */
typedef void *(*tca_int_cb_t)(void);
void set_tca_int_callback(tca_int_cb_t cb);


