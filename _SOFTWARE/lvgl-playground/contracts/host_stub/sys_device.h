#pragma once
/* Host stand-in for the firmware's sys_device.h / sys_event.h / utils.h: only what the contract headers use, copied from
 * the firmware (components/sys/sys_device/registry/sys_device.h). On the board the real headers are used instead; this
 * directory is on the playground's include path only. Keep the macros identical to the firmware's. */

#include <stdbool.h>
#include <stdint.h>

#define MUST_USE __attribute__((warn_unused_result))
typedef int err_h;   /* the firmware's error handle */

#define SYS_DEVICE_ID_NONE 0xFFFFu

#define SYS_DEVICE_OP(contract, n) ((uint16_t)(((contract) << 8) | (n)))

#define SYS_DEVICE_CONTRACT_HBRIDGE 0x09u
#define SYS_DEVICE_CONTRACT_DISPLAY 0x0Au   /* to add to the firmware's list */
#define SYS_DEVICE_CONTRACT_TOUCH   0x0Bu   /* to add to the firmware's list */
#define SYS_DEVICE_CONTRACT_OWN     0xFFu

#define SYS_DEVICE_OWN_OP(n) SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_OWN, n)

typedef struct __attribute__((packed)) {
} sys_device_args_none_t;

typedef struct __attribute__((packed)) {
} sys_device_response_empty_t;

#define SYS_DEVICE__ID(P, name, n, key_t, value_t) P##_OP_##name = SYS_DEVICE_OP(P##_CONTRACT, n),
#define SYS_DEVICE__TYPES(P, name, n, key_t, value_t) \
  typedef key_t P##_key_##name##_t;                   \
  typedef value_t P##_value_##name##_t;
#define SYS_DEVICE_CONTRACT_DECLARE(P, contract)                         \
  enum { P##_CONTRACT = (contract), P##_OPERATIONS(SYS_DEVICE__ID, P) }; \
  P##_OPERATIONS(SYS_DEVICE__TYPES, P)

#define SYS_EVENT_NOTIFY 0x01u
#define SYS_EVENT_LOCAL  0x02u
#define SYS_EVENT__ID(P, name, n, flags) \
  P##_EV_##name = SYS_DEVICE_OP(P##_CONTRACT, n), P##_EV_##name##_FLAGS = (flags),
#define SYS_EVENTS_DECLARE(P) enum { P##_EVENTS(SYS_EVENT__ID, P) };
