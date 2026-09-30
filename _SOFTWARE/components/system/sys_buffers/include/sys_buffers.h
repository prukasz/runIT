#pragma once
#include "utils.h"
#include "sys_error.h"

/**
 * @brief Variable-length item ring buffer.
 *
 * Every item is queued and dequeued whole (RINGBUF_TYPE_NOSPLIT) - the buffer
 * never coalesces or splits items, so message boundaries are always preserved.
 */
typedef struct {
  RingbufHandle_t buff;
} sys_buff_t;

/** @brief Bytes a no-split ESP ringbuffer stores in front of every item. */
#define SYS_BUFF_ITEM_HDR_LEN 8u

/**
 * @brief Buffer size (bytes) that holds @p count items of up to @p item_max bytes each.
 *
 * A no-split ESP ringbuffer stores each item 4-byte aligned behind an 8-byte
 * header and accepts one item of at most half its size minus that header, so
 * the size always covers at least two items. A buffer sized in plain bytes
 * (e.g. 512) takes items of only ~half that (248 B).
 *
 * @code
 * SE_TRY(sys_buff_init(&b, SYS_BUFF_SIZE_FOR(CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX, 4)));
 * @endcode
 */
#define SYS_BUFF_SIZE_FOR(item_max, count) \
  (((((size_t)(item_max) + 3u) & ~(size_t)3u) + SYS_BUFF_ITEM_HDR_LEN) * ((count) < 2u ? 2u : (size_t)(count)))

/**
 * @brief Allocate ringbuffer memory for a buffer descriptor.
 *
 * @param buff Pointer to a sys_buff_t descriptor to initialize.
 * @param size Ringbuffer memory allocation size in bytes.
 * @return err_h NULL on success, or ERR_NO_MEM / ERR_INVALID_SIZE on failure.
 */
SE_MUST_USE err_h sys_buff_init(sys_buff_t* buff, size_t size);

/**
 * @brief Deallocate and release the ringbuffer memory of a buffer descriptor.
 *
 * @param buff Pointer to sys_buff_t descriptor.
 * @return err_h NULL on success.
 */
SE_MUST_USE err_h sys_buff_free(sys_buff_t* buff);

/**
 * @brief Enqueue one variable-length item into the buffer.
 *
 * @param buff Pointer to the buffer descriptor.
 * @param data Pointer to the item payload.
 * @param len Length of the item payload.
 * @param wait_ms Milliseconds to wait for space if the buffer is full.
 * @return err_h NULL on success, or ERR_NO_MEM if the item didn't fit within wait_ms.
 */
SE_MUST_USE err_h sys_buff_push(sys_buff_t* buff, const void* data, size_t len, uint32_t wait_ms);

/**
 * @brief Dequeue one item verbatim.
 *
 * An item longer than max_size is never truncated: it is removed from the
 * buffer and ERR_BUFFERS_ITEM_TOO_LONG is returned, so a consumer can't
 * mistake a cut-off frame for a whole one.
 *
 * @param buff Pointer to the buffer descriptor.
 * @param buffer Pointer to the destination byte buffer.
 * @param max_size Maximum size of the destination byte buffer.
 * @param out_len Pointer to store the resulting item length.
 * @return err_h NULL on success, ERR_BASE_NOT_FOUND if empty, ERR_BUFFERS_ITEM_TOO_LONG
 *               (item dropped) if it doesn't fit in max_size, ERR_INVALID_VAL_UI32 if max_size < 1.
 */
SE_MUST_USE err_h sys_buff_pop(sys_buff_t* buff, uint8_t* buffer, size_t max_size, size_t* out_len);

/**
 * @brief Largest single item the buffer accepts (0 for an unallocated buffer).
 */
size_t sys_buff_max_item(const sys_buff_t* buff);

/**
 * @brief Remove all items currently stored in the buffer.
 *
 * @param buff Pointer to the buffer descriptor.
 */
void sys_buff_clear(sys_buff_t* buff);
