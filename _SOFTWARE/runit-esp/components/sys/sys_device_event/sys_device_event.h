#pragma once

#include "sys_device_event_id.h"
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

// The contract's only op, a write with a tail (OP_DECLARE_ON_EVENT, OP_ENTRY_W(CONTRACT_EVENT, OP_ON_EVENT)). Args = sys_event_t,
// then the payload (the call is as long as the event: the 20 B header and its `len` bytes).
#define CONTRACT_EVENT_OPERATIONS(X, P) \
  X(P, OP_ON_EVENT, 0x00, cmd_none_t, sys_event_t, SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_TAIL)

SYS_DEVICE_CONTRACT_DECLARE(CONTRACT_EVENT, MODULE_ID_EVENT)

/**
 * @brief Handler for a device op that receives an event (OP_DECLARE_WRITE_TAIL with args = sys_event_t and tail = event->len
 * bytes, checking tail_len == args->len else ERR_CMD_BAD_LEN). Used for EVENT/ON_EVENT (OP_DECLARE_ON_EVENT) and for
 * a driver's own pin/event ops subscribed via sys_device_event_subscribe_op() or sys_io_claim*().
 * @usage OP_DECLARE_EVENT(OWN_TCA6424A, OP_INT) { if (sys_device_self_suspended()) { return NULL; } ... }
 */
#define OP_DECLARE_EVENT(P, name)                                                                            \
  static err_h P##_w_##name##_event(SYS_DEVICE_CTX* c, const sys_event_t* event);                            \
  OP_DECLARE_WRITE_TAIL(P, name) {                                                                           \
    ERR_CHECK(tail_len == args->len, ERR_CMD_BAD_LEN, .got = (uint16_t)(sizeof(sys_event_t) + tail_len),     \
              .need = (uint16_t)(sizeof(sys_event_t) + args->len));                                          \
    return P##_w_##name##_event(c, args);                                                                    \
  }                                                                                                          \
  static err_h P##_w_##name##_event(SYS_DEVICE_CTX* c CMD_UNUSED, const sys_event_t* event CMD_UNUSED)

/**
 * @brief A driver's ON_EVENT handler: OP_DECLARE_EVENT(CONTRACT_EVENT, OP_ON_EVENT). The row in the op table is
 * OP_ENTRY_W(CONTRACT_EVENT, OP_ON_EVENT).
 * @usage OP_DECLARE_ON_EVENT { if (event->key.tag == IO_EV_FALLING) { return read_inputs(c); } return NULL; }
 */
#define OP_DECLARE_ON_EVENT OP_DECLARE_EVENT(CONTRACT_EVENT, OP_ON_EVENT)

/**
 * @brief Adds device `device` as an inline listener delivering matching events to `op` of its current generation
 * (sys_device_deliver): wait for the lock and skip cleanly if the device is gone or another device holds the ID.
 * `op` is either the device's own op (SYS_DEVICE_CONTRACT_OWN, LOCAL + TAIL + ALWAYS if it should run suspended) or
 * CONTRACT_EVENT_OP_ON_EVENT.
 * @param[in]  filter Which events: a key, each field exact or SYS_EVENT_ANY_*
 * @param[in]  device The listener device (already created or inside its create())
 * @param[in]  op     Target write op
 * @param[out] handle Its handle, for sys_event_unsubscribe()
 * @return ERR_EVENT_BAD_ARG for bad device, generation 0, or bad op; else as sys_event_subscribe
 */
MUST_USE err_h sys_device_event_subscribe_op(sys_event_key_t filter, uint8_t device, uint16_t op, uint16_t* handle);

/**
 * @brief Adds device `device` as an inline listener to CONTRACT_EVENT_OP_ON_EVENT: sys_device_event_subscribe_op() with
 * `op = CONTRACT_EVENT_OP_ON_EVENT`.
 * @param[in]  filter Which events: a key, each field exact or SYS_EVENT_ANY_*
 * @param[in]  device The listener device
 * @param[out] handle Its handle, for sys_event_unsubscribe()
 * @return As sys_device_event_subscribe_op
 */
MUST_USE err_h sys_device_event_subscribe(sys_event_key_t filter, uint8_t device, uint16_t* handle);

#ifdef __cplusplus
}
#endif
