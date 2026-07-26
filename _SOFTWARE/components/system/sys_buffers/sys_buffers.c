#include "sys_buffers.h"
#include <esp_log.h>
#include <string.h>
#include "sys_error.h"
#include "utils.h"

static const char* TAG = __FILE_NAME__;

#undef OWNER
#define OWNER OWNER_SYS_BUFF_INIT
err_h sys_buff_init(sys_buff_t* buff, uint8_t header, size_t size) {
  SE_CHECK_NOT_NULL(buff);
  if (size == 0) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, 0, 1, UINT32_MAX);
  }

  buff->header = header;
  buff->truncated = 0;
  buff->buff = xRingbufferCreate(size, RINGBUF_TYPE_NOSPLIT);
  SE_CHECK_IF_ALLOCATED(buff->buff);

  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_BUFF_FREE
err_h sys_buff_free(sys_buff_t* buff) {
  SE_CHECK_NOT_NULL(buff);
  if (buff->buff) {
    vRingbufferDelete(buff->buff);
    buff->buff = NULL;
  }
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_BUFF_PUSH
err_h sys_buff_push(sys_buff_t* buff, const void* data, size_t len, uint32_t wait_ms) {
  SE_CHECK_NOT_NULL(buff);
  SE_CHECK_NOT_NULL(data);
  if (len == 0) return NULL;

  if (xRingbufferSend(buff->buff, data, len, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
    SE_RET_ERR(ERR_BASE_NO_MEM, len);
  }
  return NULL;
}

/* Shared by sys_buff_pop_framed()/sys_buff_pop_raw() - pops one whole item and
   copies it into buffer, optionally prefixed with buff->header. Truncates and
   counts items that don't fit past prefix within max_size. */
static err_h sys_buff_pop(sys_buff_t* buff, uint8_t* buffer, size_t max_size, bool with_header, size_t* out_len) {
  size_t prefix = with_header ? 1 : 0;
  if (max_size < prefix + 1) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, max_size, 1, UINT32_MAX);
  }

  size_t item_size = 0;
  void* item = xRingbufferReceive(buff->buff, &item_size, 0);
  if (!item) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  size_t avail = max_size - prefix;
  size_t copy_len = (item_size > avail) ? avail : item_size;
  if (item_size > avail) {
    buff->truncated++;
    ESP_LOGW(TAG, "Item truncated from %zu to %zu bytes", item_size, avail);
  }

  if (with_header) buffer[0] = buff->header;
  memcpy(&buffer[prefix], item, copy_len);
  *out_len = copy_len + prefix;

  vRingbufferReturnItem(buff->buff, item);
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_BUFF_POP_FRAMED
err_h sys_buff_pop_framed(sys_buff_t* buff, uint8_t* buffer, size_t max_size, size_t* out_len) {
  SE_CHECK_NOT_NULL(buff);
  SE_CHECK_NOT_NULL(buffer);
  SE_CHECK_NOT_NULL(out_len);
  return sys_buff_pop(buff, buffer, max_size, true, out_len);
}

#undef OWNER
#define OWNER OWNER_SYS_BUFF_POP_RAW
err_h sys_buff_pop_raw(sys_buff_t* buff, uint8_t* buffer, size_t max_size, size_t* out_len) {
  SE_CHECK_NOT_NULL(buff);
  SE_CHECK_NOT_NULL(buffer);
  SE_CHECK_NOT_NULL(out_len);
  return sys_buff_pop(buff, buffer, max_size, false, out_len);
}

#undef OWNER
