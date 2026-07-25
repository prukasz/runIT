#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <stddef.h>
#include <stdint.h>
#include "sys_error.h"

typedef struct sys_tx_buff_t {
  RingbufHandle_t buff;
  RingbufferType_t type;
  size_t const_item_size;
  uint8_t header;
} sys_tx_buff_t;

/**
 * @brief Initializer macros for fixed-size (auto-packing) and dynamic-size (variable) payload buffers.
 */
#define SYS_BUF_FIXED_ITEM(_header, _item_size) \
  ((sys_tx_buff_t){                             \
      .buff = NULL,                             \
      .type = RINGBUF_TYPE_BYTEBUF,             \
      .const_item_size = (_item_size),          \
      .header = (_header),                      \
  })

#define SYS_BUF_DYNAMIC_ITEM(_header) \
  ((sys_tx_buff_t){                   \
      .buff = NULL,                   \
      .type = RINGBUF_TYPE_NOSPLIT,   \
      .const_item_size = 0,           \
      .header = (_header),            \
  })

/**
 * @brief Allocate ringbuffer memory for a transmit buffer descriptor.
 *
 * Preferred error-reporting initialization API.
 *
 * @param tx_buff Pointer to a sys_tx_buff_t descriptor template (initialized via SYS_BUF_*_ITEM).
 * @param size Ringbuffer memory allocation size in bytes.
 * @return err_h NULL on success, or ERR_NO_MEM / ERR_INVALID_SIZE on failure.
 */
err_h sys_buff_init(sys_tx_buff_t* tx_buff, size_t size);

/**
 * @brief Deallocate and release the ringbuffer memory of a transmit buffer descriptor.
 *
 * @param tx_buff Pointer to sys_tx_buff_t descriptor.
 * @return err_h NULL on success.
 */
err_h sys_buff_free(sys_tx_buff_t* tx_buff);

/**
 * @brief One-liner value-returning buffer initialization helper.
 *
 * Example: sys_tx_buff_t buff = sys_buff_tx_init(SYS_BUF_DYNAMIC_ITEM(0xFF), 1024);
 *
 * @param item Template sys_tx_buff_t struct created by SYS_BUF_DYNAMIC_ITEM or SYS_BUF_FIXED_ITEM.
 * @param size Capacity of ringbuffer in bytes.
 * @return sys_tx_buff_t Populated struct with allocated ringbuffer handle (or NULL on allocation failure).
 */
sys_tx_buff_t sys_buff_tx_init(sys_tx_buff_t item, size_t size);

/**
 * @brief Prepare a TX payload from a sys_tx_buff_t ring buffer.
 *
 * @param tx_buff Pointer to the transmit buffer descriptor.
 * @param buffer Pointer to the destination byte buffer.
 * @param max_size Maximum size of the destination byte buffer.
 * @param out_len Pointer to store the resulting payload length.
 * @return err_h Returns NULL on success, ERR_NOT_FOUND if empty, ERR_INVALID_SIZE if payload is too large.
 */
err_h sys_buff_prepare_tx(sys_tx_buff_t* tx_buff, uint8_t* buffer, size_t max_size, size_t* out_len);
