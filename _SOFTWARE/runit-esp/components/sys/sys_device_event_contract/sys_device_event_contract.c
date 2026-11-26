#include "sys_device_event_contract.h"

#include <stdint.h>

#include "sys_device_driver.h"

/* The listener's ctx packs sys_device_target_t ([op u16][gen u8][id u8]): no pointer to a device that can go away,
 * and a late delivery never hits a new device created under the same ID. */

static err_h on_event(const sys_event_t* event, void* ctx) {
  const sys_device_target_t t = sys_device_target_unpack(ctx);
  return sys_device_deliver(t.id, t.gen, t.op, event, (uint32_t)sizeof(*event) + event->len);
}

err_h sys_device_event_subscribe_op(sys_event_key_t filter, uint8_t device, uint16_t op, uint16_t* handle) {
  const uint8_t gen = sys_device_gen(device);
  ERR_CHECK(device < SYS_DEVICE_COUNT && gen != 0, ERR_BAD_ARG);
  ERR_CHECK((op >> 8) == SYS_DEVICE_CONTRACT_OWN || op == CONTRACT_EVENT_OP_ON_EVENT, ERR_BAD_ARG);
  void* ctx = sys_device_target_pack((sys_device_target_t){.id = device, .gen = gen, .op = op});
  return sys_event_subscribe(filter, on_event, ctx, SYS_EVENT_RUN_INLINE, handle);
}

err_h sys_device_event_subscribe(sys_event_key_t filter, uint8_t device, uint16_t* handle) {
  return sys_device_event_subscribe_op(filter, device, CONTRACT_EVENT_OP_ON_EVENT, handle);
}
