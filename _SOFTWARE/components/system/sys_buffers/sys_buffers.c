#include "sys_buffers.h"
#include <esp_log.h>
#include <string.h>
#include "utils.h"

static const char* TAG = __FILE_NAME__;

#ifndef OWNER_SYS_BUFFERS
#define OWNER_SYS_BUFFERS 0
#endif

#define OWNER OWNER_SYS_BUFFERS

status_rep_t sys_buff_init(sys_tx_buff_t* tx_buff, size_t size) {
  CHECK_NOT_NULL_R(tx_buff);
  if (size == 0) {
    return STA_C(ERR_INVALID_SIZE, OWNER, 0, STATUS_PAYLOAD_UNKNOWN);
  }

  tx_buff->buff = xRingbufferCreate(size, tx_buff->type);
  if (!tx_buff->buff) {
    return STA_C(ERR_NO_MEM, OWNER, size, STATUS_PAYLOAD_UNKNOWN);
  }

  return STA_OK;
}

status_rep_t sys_buff_free(sys_tx_buff_t* tx_buff) {
  CHECK_NOT_NULL_R(tx_buff);
  if (tx_buff->buff) {
    vRingbufferDelete(tx_buff->buff);
    tx_buff->buff = NULL;
  }
  return STA_OK;
}

sys_tx_buff_t sys_buff_tx_init(sys_tx_buff_t item, size_t size) {
  item.buff = (size > 0) ? xRingbufferCreate(size, item.type) : NULL;
  return item;
}

status_rep_t sys_buff_prepare_tx(sys_tx_buff_t* tx_buff, uint8_t* buffer, size_t max_size, size_t* out_len) {
  CHECK_NOT_NULL_R(tx_buff);
  CHECK_NOT_NULL_R(buffer);
  CHECK_NOT_NULL_R(out_len);

  if (max_size < 2) {
    return STA_C(ERR_INVALID_SIZE, OWNER, max_size, STATUS_PAYLOAD_UNKNOWN);
  }

  if (tx_buff->type == RINGBUF_TYPE_NOSPLIT) {
    size_t item_size = 0;
    void* item = xRingbufferReceive(tx_buff->buff, &item_size, 0);
    if (!item) {
      return STA_C(ERR_NOT_FOUND, OWNER, 0, STATUS_PAYLOAD_UNKNOWN);
    }

    size_t copy_len = (item_size > max_size - 1) ? (max_size - 1) : item_size;
    if (item_size > max_size - 1) {
      ESP_LOGW(TAG, "NOSPLIT item truncated from %zu to %zu bytes", item_size, max_size - 1);
    }

    buffer[0] = tx_buff->header;
    memcpy(&buffer[1], item, copy_len);
    *out_len = copy_len + 1;

    vRingbufferReturnItem(tx_buff->buff, item);
    return STA_OK;

  } else if (tx_buff->type == RINGBUF_TYPE_BYTEBUF) {
    size_t item_size = tx_buff->const_item_size;
    if (item_size == 0) {
      return STA_C(ERR_INVALID_STATE, OWNER, 0, STATUS_PAYLOAD_UNKNOWN);
    }

    size_t max_bytes = ((max_size - 1) / item_size) * item_size;
    if (max_bytes == 0) {
      return STA_C(ERR_INVALID_SIZE, OWNER, max_size, STATUS_PAYLOAD_UNKNOWN);
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
      return STA_C(ERR_NOT_FOUND, OWNER, 0, STATUS_PAYLOAD_UNKNOWN);
    }

    *out_len = 1 + total;
    return STA_OK;
  }

  return STA_C(ERR_INVALID_ARG, OWNER, tx_buff->type, STATUS_PAYLOAD_UNKNOWN);
}

#undef OWNER
