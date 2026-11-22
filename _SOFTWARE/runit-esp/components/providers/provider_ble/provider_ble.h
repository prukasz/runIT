#pragma once

#include <stdint.h>

/**
 * @file provider_ble.h
 * @brief The CREATE config of provider_ble (PROVIDER_BLE.MD): the only thing the driver publishes. Whoever makes a
 * BLE provider (app/runit, the board's record) fills it; the driver is reached through the registry, never called.
 */

//#ref-defines provider_ble_flag @alias BLE provider flags @prefix PROVIDER_BLE_
#define PROVIDER_BLE_FRAMED (1u << 0)  //@alias Framed @description The characteristic carries frames (a session's way); else raw bytes

/** @brief CREATE cfg, 22 B: the service and the characteristic a provider's frames go out on and come in from. */
typedef struct __attribute__((packed)) {
  uint16_t svc;      /**< The service it is in: made here when it is new */
  uint16_t chr;      /**< The characteristic: notify out (with a TX buffer), written by the client in */
  uint8_t flags;     /**< PROVIDER_BLE_FRAMED, or 0 */
  uint8_t rx_flags;  /**< CMD_SEND_* of each write out of the port (CMD_SEND_NOWAIT: the BLE host never waits) */
  char name[16];     /**< The user description of the characteristic (descriptor 0x2901), NUL-ended; "" = none */
} provider_ble_cfg_t;

_Static_assert(sizeof(provider_ble_cfg_t) == 22u, "provider_ble CREATE cfg");
