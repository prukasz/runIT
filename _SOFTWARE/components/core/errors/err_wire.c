#include <string.h>
#include "errors.h"

#define WIRE_TRUNCATED 0x80u
#define WIRE_NODE_HDR 6u  // tag u32 + sev u8 + len u8

uint32_t err_encode(err_h chain, uint8_t* buf, uint32_t cap) {
  if (buf == NULL || cap == 0) {
    return 0;
  }

  uint32_t pos = 1;  // buf[0] = node count, written last
  uint8_t count = 0;
  bool truncated = false;
  uint32_t left = CONFIG_ERRORS_POOL_BLOCKS + 1;  // cycle guard

  for (err_h e = chain; e != NULL && left-- > 0; e = e->next) {
    const err_tag_info_t* info = err_tag_info(e->tag);
    uint8_t len = info != NULL ? info->size : 0;
    if (pos + WIRE_NODE_HDR + len > cap || count == WIRE_TRUNCATED - 1) {
      truncated = true;
      break;
    }
    buf[pos++] = (uint8_t)e->tag;
    buf[pos++] = (uint8_t)(e->tag >> 8);
    buf[pos++] = (uint8_t)(e->tag >> 16);
    buf[pos++] = (uint8_t)(e->tag >> 24);
    buf[pos++] = err_sev(e);
    buf[pos++] = len;
    memcpy(&buf[pos], e->data, len);
    pos += len;
    count++;
  }

  buf[0] = (uint8_t)(count | (truncated ? WIRE_TRUNCATED : 0));
  return pos;
}
