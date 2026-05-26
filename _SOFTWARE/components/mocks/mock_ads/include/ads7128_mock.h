#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/**
 * @brief Simulate voltage on a specific ADS7128 pin.
 */
void ads_mock_simulate_voltage(uint8_t pin, uint16_t voltage);

/**
 * @brief Get the current state of the ALERT pin.
 * @return 0 if active (LOW), 1 if inactive (HIGH)
 */
int ads_mock_get_alert_pin_level(void);


/**
 * @brief Set the callback for ALERT pin changes.
 */
void ads_mock_add_alert_callback(void (*cb)(void*), void* arg);

/**
 * @brief Mock I2C transmit function for ADS7128.
 */
esp_err_t ads_mock_transmit(i2c_master_dev_handle_t handle, const uint8_t *write_buffer, size_t write_buffer_len, int xfer_timeout_ms);

/**
 * @brief Mock I2C transmit_receive function for ADS7128.
 */
esp_err_t ads_mock_transmit_receive(i2c_master_dev_handle_t handle, const uint8_t *write_buffer, size_t write_buffer_len, uint8_t *read_buffer, size_t read_buffer_len, int xfer_timeout_ms);
