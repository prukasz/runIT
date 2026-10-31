#include "cmd.h"

ERR_MODULE_DEFINE(ERR_MOD_CMD, "cmd", ERR_CMD_MAP)

err_h cmd_side(cmd_f read, cmd_f write, uint8_t dir, cmd_f* fn) {
  *fn = dir == CMD_DIR_READ ? read : dir == CMD_DIR_WRITE ? write : NULL;
  ERR_CHECK(*fn != NULL, ERR_CMD_DIRECTION, .dir = dir);
  return NULL;
}

err_h cmd_reply(cmd_out_t* out, const void* data, uint32_t len) {
  ERR_CHECK(len <= out->cap, ERR_CMD_OUT_FULL, .need = (uint16_t)len, .cap = (uint16_t)out->cap);
  memcpy(out->data, data, len);
  out->len = len;
  return NULL;
}

err_h cmd_append(cmd_out_t* out, const void* data, uint32_t len) {
  ERR_CHECK(out->len + len <= out->cap, ERR_CMD_OUT_FULL, .need = (uint16_t)(out->len + len),
            .cap = (uint16_t)out->cap);
  memcpy(out->data + out->len, data, len);
  out->len += len;
  return NULL;
}
