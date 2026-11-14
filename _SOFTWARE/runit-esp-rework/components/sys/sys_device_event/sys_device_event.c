#include "sys_device_event.h"

#include <stdint.h>

#include "sys_device_driver.h"

SYS_DEVICE_CONTRACT_DEFINE(EVENT, 0x03)

/* The listener's ctx packs [op u16][gen u8][owner u8] like a device timer: no pointer to a device that can go away,
 * and a late delivery never hits a new device created under the same ID. */
_Static_assert(sizeof(uintptr_t) >= sizeof(uint32_t), "an event ctx holds [op u16][gen u8][owner u8]");

#define CTX_PACK(owner, gen, op) ((void*)(uintptr_t)(((uint32_t)(op) << 16) | ((uint32_t)(gen) << 8) | (owner)))
#define CTX_OWNER(ctx)           ((uint8_t)((uintptr_t)(ctx) & 0xFFu))
#define CTX_GEN(ctx)             ((uint8_t)(((uintptr_t)(ctx) >> 8) & 0xFFu))
#define CTX_OP(ctx)              ((uint16_t)((uintptr_t)(ctx) >> 16))

static err_h on_event(const sys_event_t* event, void* ctx) {
  return sys_device_deliver(CTX_OWNER(ctx), CTX_GEN(ctx), CTX_OP(ctx), event, (uint32_t)sizeof(*event) + event->len);
}

err_h sys_device_event_subscribe_op(sys_event_key_t filter, uint8_t device, uint16_t op, uint16_t* handle) {
  const uint8_t gen = sys_device_gen(device);
  ERR_CHECK(device < SYS_DEVICE_COUNT && gen != 0, ERR_EVENT_BAD_ARG);
  ERR_CHECK((op >> 8) == SYS_DEVICE_CONTRACT_OWN || op == CONTRACT_EVENT_OP_ON_EVENT, ERR_EVENT_BAD_ARG);
  return sys_event_subscribe(filter, on_event, CTX_PACK(device, gen, op), SYS_EVENT_RUN_INLINE, handle);
}

err_h sys_device_event_subscribe(sys_event_key_t filter, uint8_t device, uint16_t* handle) {
  return sys_device_event_subscribe_op(filter, device, CONTRACT_EVENT_OP_ON_EVENT, handle);
}
