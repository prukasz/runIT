#include "sys_event_router.h"
#include <string.h>
#include "sys_event.h"

/*   key  op           request                                                        response
 *   0x01 SUBSCRIBE    rule { u8 id, u16 source, u16 event, u8 channel, u8 action, u8 flags }  -  (same ID: replaced)
 *   0x02 UNSUBSCRIBE  { u8 id }  (0xFF = every rule)                                  -
 *   0x03 LIST         { u8 start }                                                   [u8 total] then rules by ascending ID
 *                                                                                    from `start`, as many as fit
 *   0x04 PUBLISH      { u16 source, u16 event, u8 channel, i32 value, u8 flags }     -  (a first-hand event, for tests)
 *
 *   Match: source 0xFFFF = any, event 0xFFFF = any, event low byte 0xFF = any of that contract, channel 0xFF = any.
 *   Rule flags bit 0: the action runs urgent. */

#define OP_SUBSCRIBE 0x01u
#define OP_UNSUBSCRIBE 0x02u
#define OP_LIST 0x03u
#define OP_PUBLISH 0x04u

typedef struct __attribute__((packed)) {
  uint8_t id;
  uint16_t source;
  uint16_t event;
  uint8_t channel;
  uint8_t action;
  uint8_t flags;
} rule_wire_t;

typedef struct __attribute__((packed)) {
  uint8_t id;
} id_args_t;

typedef struct __attribute__((packed)) {
  uint16_t source;
  uint16_t event;
  uint8_t channel;
  int32_t value;
  uint8_t flags;
} publish_t;

ROUTER_OP(op_subscribe, rule_wire_t) {
  const sys_event_rule_t r = {.id = a->id, .source = a->source, .event = a->event, .channel = a->channel,
                              .action = a->action, .flags = a->flags};
  return sys_event_rule_set(&r);
}

ROUTER_OP(op_unsubscribe, id_args_t) {
  return sys_event_rule_remove(a->id);
}

typedef struct {
  uint8_t* out;
  uint32_t cap;
  uint32_t pos;
} list_page_t;

// Adds one rule while it fits; the first one that doesn't ends the page.
static bool list_put(const sys_event_rule_t* r, void* ctx) {
  list_page_t* p = ctx;
  if (p->pos + sizeof(rule_wire_t) > p->cap) {
    return false;
  }
  const rule_wire_t w = {.id = r->id, .source = r->source, .event = r->event, .channel = r->channel,
                         .action = r->action, .flags = r->flags};
  memcpy(p->out + p->pos, &w, sizeof(w));
  p->pos += sizeof(w);
  return true;
}

ROUTER_OP(op_list, id_args_t) {
  ERR_CHECK(out_cap >= 1, ERR_CMD_OUT_FULL, .need = 1, .cap = 0);
  list_page_t page = {.out = out, .cap = out_cap, .pos = 1};
  uint8_t total;
  ERR_TRY(sys_event_rule_list(a->id, list_put, &page, &total));
  out[0] = total;
  *out_len = page.pos;
  return NULL;
}

ROUTER_OP(op_publish, publish_t) {
  const sys_event_t e = {
      .source = a->source, .event = a->event, .channel = a->channel, .value = a->value, .flags = a->flags};
  return sys_event_publish(&e);
}

static const router_entry_t k_ops[] = {
    {OP_SUBSCRIBE, op_subscribe},
    {OP_UNSUBSCRIBE, op_unsubscribe},
    {OP_LIST, op_list},
    {OP_PUBLISH, op_publish},
};

ROUTER_MODULE(sys_event_router, k_ops)
