#include "cmd.h"
#include <string.h>

ERR_MODULE_DEFINE(ERR_MOD_CMD, "cmd", ERR_CMD_MAP)

err_h cmd_reply(uint8_t* out, uint32_t out_cap, uint32_t* out_len, const void* data, uint32_t len) {
  ERR_CHECK(len <= out_cap, ERR_CMD_OUT_FULL, .need = (uint16_t)len, .cap = (uint16_t)out_cap);
  memcpy(out, data, len);
  *out_len = len;
  return NULL;
}

err_h cmd_append(uint8_t* out, uint32_t cap, uint32_t* pos, const void* data, uint32_t len) {
  ERR_CHECK(*pos + len <= cap, ERR_CMD_OUT_FULL, .need = (uint16_t)(*pos + len), .cap = (uint16_t)cap);
  memcpy(out + *pos, data, len);
  *pos += len;
  return NULL;
}

err_h cmd_name_arg(const uint8_t* tail, uint32_t tail_len, const char** name) {
  ERR_CHECK(tail_len > 0 && tail[tail_len - 1] == '\0' && strlen((const char*)tail) == tail_len - 1, ERR_CMD_BAD_ARG);
  *name = (const char*)tail;
  return NULL;
}
