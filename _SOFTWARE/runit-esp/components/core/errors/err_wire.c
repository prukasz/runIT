#include <sys/time.h>

#include "err_internal.h"

#define RECORD_INTERNAL_HDR (1u + ERR_RECORD_STAMP_BYTES)  // src, stamp
#define RECORD_COMMAND_HDR (RECORD_INTERNAL_HDR + 4u)      // ... link, seq, flags, req_len
#define CHAIN_CUT 0x80u                                    // bit 7 of the node count: nodes were left out
#define NODE_HDR 5u                                        // tag u32 + len u8

_Static_assert(CONFIG_ERRORS_RECORD_REQ_MAX < ERR_RECORD_REQ_CUT, "ERRORS_RECORD_REQ_MAX must fit 7 bits");

uint32_t err_record(const err_cmd_t* cmd, err_h chain, uint8_t* buf, uint32_t cap) {
  // The header and the chain count.
  if (buf == NULL || cap < (cmd == NULL ? RECORD_INTERNAL_HDR : RECORD_COMMAND_HDR) + 1) {
    return 0;
  }
  // The time of the report: the system clock, UTC in ms (it counts from the start until a client sets it).
  struct timeval tv;
  gettimeofday(&tv, NULL);
  const uint64_t now_ms = (uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u;
  uint32_t pos = 0;
  buf[pos++] = cmd == NULL ? ERR_RECORD_SRC_INTERNAL : ERR_RECORD_SRC_COMMAND;
  memcpy(buf + pos, &now_ms, ERR_RECORD_STAMP_BYTES);  // little-endian target
  pos += ERR_RECORD_STAMP_BYTES;

  if (cmd != NULL) {
    // Request copy: at most REQ_MAX, and leave one byte for the chain count.
    const uint32_t room = cap - RECORD_COMMAND_HDR - 1;
    const uint32_t req_max = room < CONFIG_ERRORS_RECORD_REQ_MAX ? room : CONFIG_ERRORS_RECORD_REQ_MAX;
    const uint32_t req_copy = cmd->req_len < req_max ? cmd->req_len : req_max;
    buf[pos++] = cmd->link;
    buf[pos++] = cmd->seq;
    buf[pos++] = cmd->flags;
    buf[pos++] = (uint8_t)(req_copy | (req_copy < cmd->req_len ? ERR_RECORD_REQ_CUT : 0));
    if (req_copy > 0) {
      memcpy(buf + pos, cmd->req, req_copy);
      pos += req_copy;
    }
  }

  // The chain, outermost node first: [count] then [tag u32 LE][len][payload] per node; what doesn't fit is cut.
  const uint32_t count_at = pos++;
  uint8_t count = 0;
  bool cut = false;
  uint32_t left = CONFIG_ERRORS_POOL_BLOCKS + 1;  // cycle guard
  for (err_h e = chain; e != NULL && left-- > 0; e = e->next) {
    const err_tag_info_t* info = err_tag_info(e->tag);
    const uint8_t len = info != NULL ? info->size : 0;
    if (pos + NODE_HDR + len > cap || count == CHAIN_CUT - 1) {
      cut = true;
      break;
    }
    memcpy(buf + pos, &e->tag, sizeof(uint32_t));  // little-endian target
    pos += sizeof(uint32_t);
    buf[pos++] = len;
    memcpy(buf + pos, e->data, len);
    pos += len;
    count++;
  }
  buf[count_at] = (uint8_t)(count | (cut ? CHAIN_CUT : 0));
  return pos;
}
