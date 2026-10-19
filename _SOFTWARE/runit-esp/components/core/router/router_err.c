#include <sys/time.h>

#include "router.h"
#include "sdkconfig.h"

#define INTERNAL_HDR_BYTES (1u + ROUTER_ERR_STAMP_BYTES)  // src, stamp
#define COMMAND_HDR_BYTES (INTERNAL_HDR_BYTES + 4u)      // ... link, seq, flags, req_len

_Static_assert(CONFIG_ROUTER_ERR_REQ_MAX < ROUTER_ERR_REQ_CUT, "ROUTER_ERR_REQ_MAX must fit 7 bits");

uint32_t router_err_record(const router_cmd_t* cmd, err_h err, uint8_t* buf, uint32_t cap) {
  // The header and the chain count.
  if (buf == NULL || cap < (cmd == NULL ? INTERNAL_HDR_BYTES : COMMAND_HDR_BYTES) + 1) {
    return 0;
  }
  // The time of the report: the system clock, UTC in ms (it counts from the start until a client sets it).
  struct timeval tv;
  gettimeofday(&tv, NULL);
  const uint64_t now_ms = (uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u;
  uint32_t pos = 0;
  buf[pos++] = cmd == NULL ? ROUTER_ERR_SRC_INTERNAL : ROUTER_ERR_SRC_COMMAND;
  memcpy(buf + pos, &now_ms, ROUTER_ERR_STAMP_BYTES);  // little-endian target
  pos += ROUTER_ERR_STAMP_BYTES;
  if (cmd == NULL) {
    return pos + err_encode(err, buf + pos, cap - pos);
  }

  // Request copy: at most REQ_MAX, and leave one byte for the chain count.
  uint32_t room = cap - COMMAND_HDR_BYTES - 1;
  uint32_t req_copy = cmd->req_len;
  if (req_copy > CONFIG_ROUTER_ERR_REQ_MAX) {
    req_copy = CONFIG_ROUTER_ERR_REQ_MAX;
  }
  if (req_copy > room) {
    req_copy = room;
  }

  buf[pos++] = cmd->link;
  buf[pos++] = cmd->seq;
  buf[pos++] = cmd->flags;
  buf[pos++] = (uint8_t)(req_copy | (req_copy < cmd->req_len ? ROUTER_ERR_REQ_CUT : 0));
  if (req_copy > 0) {
    memcpy(buf + pos, cmd->req, req_copy);
    pos += req_copy;
  }
  return pos + err_encode(err, buf + pos, cap - pos);
}
