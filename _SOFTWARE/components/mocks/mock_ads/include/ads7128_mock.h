#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/**
 * @brief Initialize the ADS7128 mock simulator task.
 */
void init_ads_mock(void);

/**
 * @brief Simulate voltage on a specific ADS7128 pin.
 */
void ads_simulate_voltage(uint8_t pin, uint16_t voltage);

/**
 * @brief Get the current state of the ALERT pin.
 * @return 0 if active (LOW), 1 if inactive (HIGH)
 */
int ads_get_alert_pin_level(void);

typedef void (*ads_alert_cb_t)(void);

/**
 * @brief Set the callback for ALERT pin changes.
 */
void set_ads_alert_callback(ads_alert_cb_t cb);

/**
 * @brief Mock I2C transmit function for ADS7128.
 */
esp_err_t ads_transmit(i2c_master_dev_handle_t handle, const uint8_t *write_buffer, size_t write_buffer_len, int xfer_timeout_ms);

/**
 * @brief Mock I2C transmit_receive function for ADS7128.
 */
esp_err_t ads_transmit_receive(i2c_master_dev_handle_t handle, const uint8_t *write_buffer, size_t write_buffer_len, uint8_t *read_buffer, size_t read_buffer_len, int xfer_timeout_ms);
