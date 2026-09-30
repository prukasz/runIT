#include "sys_buffers.h"

#undef OWNER
#define OWNER OWNER_SYS_BUFF_INIT
err_h sys_buff_init(sys_buff_t* buff, size_t size) {
  SE_CHECK_NOT_NULL(buff);
  SE_CHECK_IN_RANGE(size, 1, UINT32_MAX);

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
    SE_FAIL(ERR_BASE_NO_MEM, len);
  }
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_BUFF_POP_RAW
err_h sys_buff_pop(sys_buff_t* buff, uint8_t* buffer, size_t max_size, size_t* out_len) {
  SE_CHECK_NOT_NULL(buff);
  SE_CHECK_NOT_NULL(buffer);
  SE_CHECK_NOT_NULL(out_len);
  SE_CHECK_IN_RANGE(max_size, 1, UINT32_MAX);

  size_t item_size = 0;
  void* item = xRingbufferReceive(buff->buff, &item_size, 0);
  if (!item) {
    *out_len = 0;
    SE_FAIL(ERR_BASE_NOT_FOUND, 0);
  }

  if (item_size > max_size) {
    vRingbufferReturnItem(buff->buff, item);
    *out_len = 0;
    SE_FAIL(ERR_BUFFERS_ITEM_TOO_LONG, .len = (uint32_t)item_size, .max = (uint32_t)max_size);
  }

  memcpy(buffer, item, item_size);
  *out_len = item_size;

  vRingbufferReturnItem(buff->buff, item);
  return NULL;
}
#undef OWNER

size_t sys_buff_max_item(const sys_buff_t* buff) {
  return (buff && buff->buff) ? xRingbufferGetMaxItemSize(buff->buff) : 0;
}

void sys_buff_clear(sys_buff_t* buff) {
  if (!buff || !buff->buff) return;
  size_t item_size = 0;
  void* item = NULL;
  while ((item = xRingbufferReceive(buff->buff, &item_size, 0)) != NULL) {
    vRingbufferReturnItem(buff->buff, item);
  }
}
