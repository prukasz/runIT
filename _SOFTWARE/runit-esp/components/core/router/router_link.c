#include "router.h"
#include "router_priv.h"
#include "sdkconfig.h"

#define REQ_HDR_BYTES     2u    // [seq][flags]
#define RESP_HDR_BYTES    3u    // [stream][seq][status]
#define REJECT_RESP_BYTES 64u   // error answers to a frame not submitted (link task stack)

_Static_assert(RESP_HDR_BYTES <= CMD_HEAD, "an answer's header fits the room the router keeps for it");

static struct {
  const router_link_t* links[CONFIG_ROUTER_MAX_LINKS + 1];  // index = link ID, 0 unused; written at boot
  void (*on_frame)(void);
} s_links;

err_h router_link_register(uint8_t link_id, const router_link_t* link) {
  ERR_CHECK(link != NULL && link->send != NULL, ERR_CMD_BAD_ARG);
  ERR_CHECK(link_id > 0 && link_id <= CONFIG_ROUTER_MAX_LINKS, ERR_ROUTER_BAD_LINK, .link = link_id);
  ERR_CHECK(s_links.links[link_id] == NULL, ERR_ROUTER_BAD_LINK, .link = link_id);
  s_links.links[link_id] = link;
  return NULL;
}

const router_link_t* router_link_get(uint8_t link_id) {
  return link_id > 0 && link_id <= CONFIG_ROUTER_MAX_LINKS ? s_links.links[link_id] : NULL;
}

void router_link_on_frame(void (*fn)(void)) {
  s_links.on_frame = fn;
}

/* The router's done for a link's command (ctx: the link ID): [stream][seq][status] in the room in front of the data,
 * sent to the link's answer endpoint. */
static err_h answer(void* ctx, const err_cmd_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  const router_link_t* link = router_link_get((uint8_t)(uintptr_t)ctx);  // links are never removed
  uint8_t* frame = data - RESP_HDR_BYTES;
  frame[0] = CONFIG_ROUTER_RESPONSE_STREAM;
  frame[1] = cmd->seq;
  frame[2] = status;
  return link->send(link->answer_endpoint, frame, RESP_HDR_BYTES + len, true);
}

err_h router_link_receive(uint8_t link_id, const uint8_t* frame, uint32_t len) {
  const router_link_t* link = router_link_get(link_id);
  ERR_CHECK(link != NULL, ERR_ROUTER_BAD_LINK, .link = link_id);
  ERR_CHECK(frame != NULL || len == 0, ERR_CMD_BAD_ARG);
  if (s_links.on_frame != NULL) {
    s_links.on_frame();
  }

  // A short frame gives what it has.
  const err_cmd_t cmd = {
      .req = len > REQ_HDR_BYTES ? frame + REQ_HDR_BYTES : NULL,
      .req_len = len > REQ_HDR_BYTES ? len - REQ_HDR_BYTES : 0,
      .link = link_id,
      .seq = len > 0 ? frame[0] : 0,
      .flags = len > 1 ? frame[1] : 0,
  };
  err_h err = len <= CMD_FRAME_MAX ? router_submit(&cmd, answer, (void*)(uintptr_t)link_id)
                                   : ERR_NEW(ERR_ROUTER_FRAME_LEN, .link = link_id, .len = (uint16_t)len,
                                             .max = CMD_FRAME_MAX);
  if (err == NULL) {
    return NULL;
  }
  // Not queued: answered when it has a seq to answer to, and reported with its command.
  if (len > 0 && !(cmd.flags & ROUTER_FLAG_NOREPLY)) {
    uint8_t buf[REJECT_RESP_BYTES];
    uint32_t n = err_record(&cmd, err, buf + RESP_HDR_BYTES, sizeof(buf) - RESP_HDR_BYTES);
    err_report(answer((void*)(uintptr_t)link_id, &cmd, ROUTER_STATUS_ERROR, buf + RESP_HDR_BYTES, n));
  }
  router_report_cmd(&cmd, err);
  return NULL;
}
