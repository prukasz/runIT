#include "err_internal.h"
#include "board_time.h"

#define RECORD_INTERNAL_HDR (1u + ERR_RECORD_STAMP_BYTES)                     // src, stamp
#define RECORD_COMMAND_HDR  (RECORD_INTERNAL_HDR + sizeof(err_record_cmd_t))  // ... the command header
#define CHAIN_CUT           0x80u                                             // bit 7 of the node count: nodes left out
#define NODE_HDR            5u                                                // tag u32 + len u8

_Static_assert(CONFIG_ERRORS_RECORD_REQ_MAX < ERR_RECORD_REQ_CUT, "ERRORS_RECORD_REQ_MAX must fit 7 bits");

uint32_t err_record(const err_cmd_t* cmd, err_h chain, uint8_t* buf, uint32_t cap) {
  // The header and the chain count.
  if (buf == NULL || cap < (cmd == NULL ? RECORD_INTERNAL_HDR : RECORD_COMMAND_HDR) + 1) {
    return 0;
  }
  // The time of the report: the board's clock, UTC in ms (it counts from the start until a client sets it).
  const uint64_t now_ms = board_time_now_ms();
  uint32_t pos = 0;
  buf[pos++] = cmd == NULL ? ERR_RECORD_SRC_INTERNAL : ERR_RECORD_SRC_COMMAND;
  memcpy(buf + pos, &now_ms, ERR_RECORD_STAMP_BYTES);  // little-endian target
  pos += ERR_RECORD_STAMP_BYTES;

  if (cmd != NULL) {
    // Request copy: at most REQ_MAX, and leave one byte for the chain count.
    const uint32_t room = cap - RECORD_COMMAND_HDR - 1;
    const uint32_t req_max = room < CONFIG_ERRORS_RECORD_REQ_MAX ? room : CONFIG_ERRORS_RECORD_REQ_MAX;
    const uint32_t req_copy = cmd->req_len < req_max ? cmd->req_len : req_max;
    const err_record_cmd_t hdr = {
        .from = cmd->src,
        .seq = cmd->seq,
        .flags = cmd->flags,
        .req_len = (uint8_t)(req_copy | (req_copy < cmd->req_len ? ERR_RECORD_REQ_CUT : 0)),
    };
    memcpy(buf + pos, &hdr, sizeof(hdr));  // little-endian target
    pos += sizeof(hdr);
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

err_h err_record_parse(const uint8_t* data, uint32_t len, err_h* chain) {
  ERR_CHECK(chain != NULL && (data != NULL || len == 0), ERR_RECORD_CUT, .len = (uint16_t)len);
  *chain = NULL;
  // The header: the source, the stamp, and a command's header and request.
  uint32_t pos = RECORD_INTERNAL_HDR;
  ERR_CHECK(len > pos, ERR_RECORD_CUT, .len = (uint16_t)len);
  if (data[0] == ERR_RECORD_SRC_COMMAND) {
    ERR_CHECK(len > pos + sizeof(err_record_cmd_t), ERR_RECORD_CUT, .len = (uint16_t)len);
    pos += sizeof(err_record_cmd_t) + (data[pos + sizeof(err_record_cmd_t) - 1] & ~ERR_RECORD_REQ_CUT);
    ERR_CHECK(len > pos, ERR_RECORD_CUT, .len = (uint16_t)len);
  }

  // The nodes' offsets first: the chain is built from its root cause out, each node the cause of the one before.
  const uint8_t count = data[pos++] & ~CHAIN_CUT;
  uint16_t at[CHAIN_CUT - 1];
  for (uint8_t i = 0; i < count; i++) {
    ERR_CHECK(len >= pos + NODE_HDR && len >= pos + NODE_HDR + data[pos + NODE_HDR - 1], ERR_RECORD_CUT,
              .len = (uint16_t)len);
    at[i] = (uint16_t)pos;
    pos += NODE_HDR + data[pos + NODE_HDR - 1];
  }
  for (uint8_t i = count; i-- > 0;) {
    uint32_t tag;
    memcpy(&tag, data + at[i], sizeof(tag));  // little-endian target
    const uint32_t size = data[at[i] + NODE_HDR - 1] < ERR_DATA_MAX ? data[at[i] + NODE_HDR - 1] : ERR_DATA_MAX;
#if CONFIG_ERRORS_INFO
    err_h e = err_new(NULL, tag, data + at[i] + NODE_HDR, size, *chain);
#else
    err_h e = err_new(tag, data + at[i] + NODE_HDR, size, *chain);
#endif
    if (e == *chain || e->tag != tag) {
      break;  // the pool is short: the outer nodes are left out
    }
    *chain = e;
  }
  return NULL;
}
