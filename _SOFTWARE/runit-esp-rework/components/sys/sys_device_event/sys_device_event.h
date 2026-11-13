#pragma once

#include "sys_device.h"
#include "sys_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * EVENT: 0x03xx, where 0x03 is the events contract and xx its op. A device that listens to events.
 * ON_EVENT - an event delivered to the device (sys_event_t and its payload), a write. Always (runs while the device
 * is suspended)
 * No events.
 */

// The contract's only op, a write with a tail (OP_WRITE_ON_EVENT, OP_ENTRY_W(EVENT, ON_EVENT)). Args = sys_event_t,
// then the payload (the call is as long as the event: the 20 B header and its `len` bytes).
#define EVENT_OPERATIONS(X, P) \
  X(P, ON_EVENT, 0x00, cmd_none_t, sys_event_t, SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_TAIL)

MODULE_ID_DECLARE(EVENT, 0x03)
SYS_DEVICE_CONTRACT_DECLARE(EVENT, 0x03)

/**
 * @brief A driver's ON_EVENT handler: OP_WRITE_TAIL(EVENT, ON_EVENT) with the one rule an event adds, the tail is as
 * long as the event's `len` (else ERR_CMD_BAD_LEN). The body sees `c` (the driver's struct, SYS_DEVICE_CTX*) and
 * `event` (used in place; its payload SYS_EVENT_DATA(event, NAME), event->len bytes). The row in the op table is
 * OP_ENTRY_W(EVENT, ON_EVENT).
 * @usage OP_WRITE_ON_EVENT { if (event->key.tag == IO_EV_FALLING) { return read_inputs(c); } return NULL; }
 */
#define OP_WRITE_ON_EVENT                                                                                    \
  static err_h EVENT_w_ON_EVENT_event(SYS_DEVICE_CTX* c, const sys_event_t* event);                          \
  OP_WRITE_TAIL(EVENT, ON_EVENT) {                                                                           \
    ERR_CHECK(tail_len == args->len, ERR_CMD_BAD_LEN, .got = (uint16_t)(sizeof(sys_event_t) + tail_len),     \
              .need = (uint16_t)(sizeof(sys_event_t) + args->len));                                          \
    return EVENT_w_ON_EVENT_event(c, args);                                                                  \
  }                                                                                                          \
  static err_h EVENT_w_ON_EVENT_event(SYS_DEVICE_CTX* c CMD_UNUSED, const sys_event_t* event CMD_UNUSED)

/**
 * @brief Adds device `device` as an inline listener: its ON_EVENT (sys_device_write) gets the matching events, in the
 * publisher's task. The listener is the device's ID, not a pointer, so a device removed while an event is on its way
 * just answers ERR_DEV_NOT_FOUND (reported with the event). An event published inside a device op is delivered when
 * the publishing task's outermost sys_device_call is over (the registry holds it until then), so the ON_EVENT never
 * waits for a lock its own task holds. It may block on I/O (I2C), never on a delay.
 * @param[in]  filter Which events: a key, each field exact or SYS_EVENT_ANY_*
 * @param[in]  device The listener device
 * @param[out] handle Its handle, for sys_event_unsubscribe()
 * @return ERR_EVENT_BAD_ARG for SYS_DEVICE_ID_NONE, else as sys_event_subscribe
 */
MUST_USE err_h sys_device_event_subscribe(sys_event_key_t filter, uint8_t device, uint16_t* handle);

#ifdef __cplusplus
}
#endif
