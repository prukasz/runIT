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

/*
INFO:
Receive and manage (redirect) received commanads
*/

typedef struct{
    EventGroupHandle_t connection_events;
    EventBits_t connection_bits_rx;
    EventBits_t interface_bits_on_complete;
    EventBits_t interface_bits_on_error;
    EventBits_t interface_bits_on_stop;
    uint8_t task_priority;
    uint32_t task_stack_size;
}interface_cfg_t;


typedef status_rep_t (*interface_parse_func)(const uint8_t *packet_data, const uint16_t packet_len);

typedef status_rep_t (*interface_dev_cfg_func)(const uint8_t *packet_data, const uint16_t packet_len);


/**
 * @brief Redirects received command packets to appropriate config setters based on dev type
 * @param packet_data Raw data received (excluding header)
 * @param packet_len Length of the packet data
 * @note Data can be recevied either by BLE or WIFI, source is irrelevant both uses same buffer
 */
status_rep_t interface_parse_cmd_dev_cfg(const uint8_t *packet_data, const uint16_t packet_len);

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
void interface_init(interface_cfg_t *config);

/**
 * @brief Dequeues received command packets from the RX buffer
 * @param data Buffer to store the dequeued data
 * @param len Pointer to variable that holds the length of the data buffer
 */
esp_err_t interface_rx_dequeue(uint8_t* data, size_t* len);

/**
 * @brief Gets the task handle of the interface dispatcher task
 * @return TaskHandle_t of the interface dispatcher task
 */
TaskHandle_t interface_get_task_handle();