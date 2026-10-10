#include <string.h>
#include "router.h"
#include "sdkconfig.h"

#define CMD_HDR 9u  // src, link, peer u32, seq, flags, req_len

_Static_assert(CONFIG_ROUTER_ERR_REQ_MAX < ROUTER_ERR_REQ_CUT, "ROUTER_ERR_REQ_MAX must fit 7 bits");

uint32_t router_err_record(const router_cmd_t* cmd, err_h err, uint8_t* buf, uint32_t cap) {
  if (buf == NULL || cap < 1 + 1) {  // src + chain count
    return 0;
  }
  if (cmd == NULL) {
    buf[0] = ROUTER_ERR_SRC_INTERNAL;
    return 1 + err_encode(err, buf + 1, cap - 1);
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
