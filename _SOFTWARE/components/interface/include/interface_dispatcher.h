#pragma once 

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/ringbuf.h>
#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>
#include <freertos/task.h> 
#include <esp_log.h>
#include "string.h"
#include "status.h"



typedef status_rep_t (*interface_parse_func)(const uint8_t *packet_data, const uint16_t packet_len);

typedef status_rep_t (*interface_dev_cfg_func)(const uint8_t *packet_data, const uint16_t packet_len);

/**
 * @brief Registers the RX buffer for command packet reception
 * @param rx_buffer Ring buffer handle for RX data
 */
void interface_buff_register_rx(RingbufHandle_t rx_buffer);

/**
 * @brief Initializes the interface dispatcher
 * @param config Configuration structure for the interface dispatcher
 * @param connection_bits_rx Bit mask for waiting on RX data reception
 * @param interface_bits_on_complete Bit mask for indicating command processing completion
 * @param interface_bits_on_error Bit mask for indicating command processing error
 * @param interface_bits_on_stop Bit mask for indicating command processing stop (e.g. emergency stop)
 * @param task_priority Task priority for the interface dispatcher task
 * @param task_stack_size Stack size for the interface dispatcher task
 */
void interface_init();



/**
 * @brief Gets the task handle of the interface dispatcher task
 * @return TaskHandle_t of the interface dispatcher task
 */
TaskHandle_t interface_get_task_handle();