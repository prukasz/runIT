#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <stddef.h>
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_buffers.h"

/**
 * @brief Variable-length item ring buffer with an associated framing header byte.
 *
 * Every item is queued and dequeued whole (RINGBUF_TYPE_NOSPLIT) - the buffer
 * never coalesces or splits items, so message boundaries are always preserved.
 */
typedef struct {
  RingbufHandle_t buff;
  uint8_t header;      // prepended to each frame produced by sys_buff_pop_framed()
  uint32_t truncated;  // count of items truncated by sys_buff_pop_framed()/sys_buff_pop_raw()
} sys_buff_t;

/**
 * @brief Allocate ringbuffer memory for a buffer descriptor.
 *
 * @param buff Pointer to a sys_buff_t descriptor to initialize.
 * @param header Framing header byte, prepended to frames by sys_buff_pop_framed().
 * @param size Ringbuffer memory allocation size in bytes.
 * @return err_h NULL on success, or ERR_NO_MEM / ERR_INVALID_SIZE on failure.
 */
err_h sys_buff_init(sys_buff_t* buff, uint8_t header, size_t size);

/**
 * @brief Deallocate and release the ringbuffer memory of a buffer descriptor.
 *
 * @param buff Pointer to sys_buff_t descriptor.
 * @return err_h NULL on success.
 */
err_h sys_buff_free(sys_buff_t* buff);

/**
 * @brief Enqueue one variable-length item into the buffer.
 *
 * @param buff Pointer to the buffer descriptor.
 * @param data Pointer to the item payload.
 * @param len Length of the item payload.
 * @param wait_ms Milliseconds to wait for space if the buffer is full.
 * @return err_h NULL on success, or ERR_NO_MEM if the item didn't fit within wait_ms.
 */
err_h sys_buff_push(sys_buff_t* buff, const void* data, size_t len, uint32_t wait_ms);

/**
 * @brief Dequeue one item, prepending the buffer's configured header byte.
 *
 * Writes buff->header to buffer[0] and the item payload to buffer[1..out_len-1].
 * An item longer than max_size - 1 is truncated and counted in buff->truncated.
 *
 * @param buff Pointer to the buffer descriptor.
 * @param buffer Pointer to the destination byte buffer.
 * @param max_size Maximum size of the destination byte buffer (must be >= 2).
 * @param out_len Pointer to store the resulting frame length.
 * @return err_h NULL on success, ERR_NOT_FOUND if empty, ERR_INVALID_SIZE if max_size < 2.
 */
err_h sys_buff_pop_framed(sys_buff_t* buff, uint8_t* buffer, size_t max_size, size_t* out_len);

/**
 * @brief Dequeue one item verbatim, without a header byte.
 *
 * An item longer than max_size is truncated and counted in buff->truncated.
 *
 * @param buff Pointer to the buffer descriptor.
 * @param buffer Pointer to the destination byte buffer.
 * @param max_size Maximum size of the destination byte buffer.
 * @param out_len Pointer to store the resulting item length.
 * @return err_h NULL on success, ERR_NOT_FOUND if empty, ERR_INVALID_SIZE if max_size < 1.
 */
err_h sys_buff_pop_raw(sys_buff_t* buff, uint8_t* buffer, size_t max_size, size_t* out_len);
