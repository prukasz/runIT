#include "sys_device_event.h"

#include <stdint.h>

SYS_DEVICE_CONTRACT_DEFINE(EVENT, 0x03)

/* The listener of every device subscription: the device's ID rides in ctx, looked up at each delivery, so a device
 * removed meanwhile answers ERR_DEV_NOT_FOUND (reported by sys_event with the event). The write is as long as the
 * event: the header and its payload (SYS_DEVICE_WRITE would take only the header's size). */
static err_h on_event(const sys_event_t* event, void* ctx) {
  const uint8_t device = (uint8_t)(uintptr_t)ctx;
  return sys_device_write(device, EVENT_OP_ON_EVENT, event, (uint32_t)sizeof(*event) + event->len);
}

err_h sys_device_event_subscribe(sys_event_key_t filter, uint8_t device, uint16_t* handle) {
  ERR_CHECK(device != SYS_DEVICE_ID_NONE, ERR_EVENT_BAD_ARG);
  return sys_event_subscribe(filter, on_event, (void*)(uintptr_t)device, SYS_EVENT_RUN_INLINE, handle);
}
