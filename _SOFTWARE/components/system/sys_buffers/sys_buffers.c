#include "sys_buffers.h"
#include <esp_log.h>
#include <string.h>
#include "utils.h"
#include "sys_error.h"

static const char* TAG = __FILE_NAME__;

#ifndef OWNER_SYS_BUFFERS
#define OWNER_SYS_BUFFERS 0
#endif

#define OWNER OWNER_SYS_BUFFERS

err_h sys_buff_init(sys_tx_buff_t* tx_buff, size_t size) {
  SE_CHECK_NOT_NULL(tx_buff);
  if (size == 0) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, 0, 1, UINT32_MAX);
  }

  tx_buff->buff = xRingbufferCreate(size, tx_buff->type);
  SE_CHECK_IF_ALLOCATED(tx_buff->buff);

  return NULL;
}

err_h sys_buff_free(sys_tx_buff_t* tx_buff) {
  SE_CHECK_NOT_NULL(tx_buff);
  if (tx_buff->buff) {
    vRingbufferDelete(tx_buff->buff);
    tx_buff->buff = NULL;
  }
  return NULL;
}

sys_tx_buff_t sys_buff_tx_init(sys_tx_buff_t item, size_t size) {
  item.buff = (size > 0) ? xRingbufferCreate(size, item.type) : NULL;
  return item;
}

err_h sys_buff_prepare_tx(sys_tx_buff_t* tx_buff, uint8_t* buffer, size_t max_size, size_t* out_len) {
  SE_CHECK_NOT_NULL(tx_buff);
  SE_CHECK_NOT_NULL(buffer);
  SE_CHECK_NOT_NULL(out_len);

  if (max_size < 2) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, max_size, 1, UINT32_MAX);
  }

  if (tx_buff->type == RINGBUF_TYPE_NOSPLIT) {
    size_t item_size = 0;
    void* item = xRingbufferReceive(tx_buff->buff, &item_size, 0);
    if (!item) {
      SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
    }

    size_t copy_len = (item_size > max_size - 1) ? (max_size - 1) : item_size;
    if (item_size > max_size - 1) {
      ESP_LOGW(TAG, "NOSPLIT item truncated from %zu to %zu bytes", item_size, max_size - 1);
    }

    buffer[0] = tx_buff->header;
    memcpy(&buffer[1], item, copy_len);
    *out_len = copy_len + 1;

    vRingbufferReturnItem(tx_buff->buff, item);
    return NULL;

  } else if (tx_buff->type == RINGBUF_TYPE_BYTEBUF) {
    size_t item_size = tx_buff->const_item_size;
    if (item_size == 0) {
      SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
    }

    size_t max_bytes = ((max_size - 1) / item_size) * item_size;
    if (max_bytes == 0) {
      SE_RET_ERR(ERR_INVALID_VAL_UI32, max_size, 1, UINT32_MAX);
    }

    buffer[0] = tx_buff->header;
    size_t total = 0;

    while (total < max_bytes) {
      size_t recv_size = 0;
      void* item = xRingbufferReceiveUpTo(tx_buff->buff, &recv_size, 0, max_bytes - total);
      if (!item) break;

      memcpy(&buffer[1 + total], item, recv_size);
      total += recv_size;
      vRingbufferReturnItem(tx_buff->buff, item);
    }

    if (total == 0) {
      SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
    }

    *out_len = 1 + total;
    return NULL;
  }

  SE_RET_ERR(ERR_INVALID_VAL_UI32, tx_buff->type, 0, 1);
}

#undef OWNER
