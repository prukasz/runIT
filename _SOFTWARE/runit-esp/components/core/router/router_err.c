
#include <sys/time.h>

#include "router.h"
#include "sdkconfig.h"

#define STAMP ROUTER_ERR_STAMP_BYTES
#define CMD_HDR (9u + STAMP)  // src, stamp, link, peer u32, seq, flags, req_len

_Static_assert(CONFIG_ROUTER_ERR_REQ_MAX < ROUTER_ERR_REQ_CUT, "ROUTER_ERR_REQ_MAX must fit 7 bits");

uint32_t router_err_record(const router_cmd_t* cmd, err_h err, uint8_t* buf, uint32_t cap) {
  if (buf == NULL || cap < 1 + STAMP + 1) {  // src + stamp + chain count
    return 0;
  }
  // The time of the report: the system clock, UTC in ms (it counts from the start until a client sets it).
  struct timeval tv;
  gettimeofday(&tv, NULL);
  const uint64_t now_ms = (uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u;
  if (cmd == NULL) {
    buf[0] = ROUTER_ERR_SRC_INTERNAL;
    memcpy(buf + 1, &now_ms, STAMP);  // little-endian target
    return 1 + STAMP + err_encode(err, buf + 1 + STAMP, cap - 1 - STAMP);
  }
  if (cap < CMD_HDR + 1) {
    return 0;
  }

  // Request copy: at most REQ_MAX, and leave one byte for the chain count.
  uint32_t room = cap - CMD_HDR - 1;
  uint32_t req_copy = cmd->req_len;
  if (req_copy > CONFIG_ROUTER_ERR_REQ_MAX) {
    req_copy = CONFIG_ROUTER_ERR_REQ_MAX;
  }
  if (req_copy > room) {
    req_copy = room;
  }

  uint32_t pos = 0;
  buf[pos++] = ROUTER_ERR_SRC_COMMAND;
  memcpy(buf + pos, &now_ms, STAMP);
  pos += STAMP;
  buf[pos++] = cmd->link;
  buf[pos++] = (uint8_t)cmd->peer;
  buf[pos++] = (uint8_t)(cmd->peer >> 8);
  buf[pos++] = (uint8_t)(cmd->peer >> 16);
  buf[pos++] = (uint8_t)(cmd->peer >> 24);
  buf[pos++] = cmd->seq;
  buf[pos++] = cmd->flags;
  buf[pos++] = (uint8_t)(req_copy | (req_copy < cmd->req_len ? ROUTER_ERR_REQ_CUT : 0));
  if (req_copy > 0) {
    memcpy(buf + pos, cmd->req, req_copy);
    pos += req_copy;
  }
  return pos + err_encode(err, buf + pos, cap - pos);
}
