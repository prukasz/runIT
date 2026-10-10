#include "cmd.h"
#include <string.h>

ERR_MODULE_DEFINE(ERR_MOD_CMD, "cmd", ERR_CMD_MAP)

err_h cmd_reply(uint8_t* out, uint32_t out_cap, uint32_t* out_len, const void* data, uint32_t len) {
  ERR_CHECK(len <= out_cap, ERR_CMD_OUT_FULL, .need = (uint16_t)len, .cap = (uint16_t)out_cap);
  memcpy(out, data, len);
  *out_len = len;
  return NULL;
}
