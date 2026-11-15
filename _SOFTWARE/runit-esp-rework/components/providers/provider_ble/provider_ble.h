#pragma once

#include <stdint.h>

/**
 * @file provider_ble.h
 * @brief The CREATE config of provider_ble (PROVIDER_BLE.MD): the only thing the driver publishes. Whoever makes a
 * BLE provider (app/runit, the board's record) fills it; the driver is reached through the registry, never called.
 */

#define PROVIDER_BLE_FRAMED (1u << 0)  /**< Both characteristics carry frames (a session's way); else raw bytes (a stream's) */

/** @brief CREATE cfg, 23 B: the service and the characteristics a provider's frames go out on and come in from. */
typedef struct __attribute__((packed)) {
  uint16_t svc;   /**< The service they are in: made here when it is new */
  uint16_t tx;    /**< Out: notify, with a TX buffer; 0 = none */
  uint16_t rx;    /**< In: written by the client; 0 = none (at least one of tx and rx) */
  uint8_t flags;  /**< PROVIDER_BLE_FRAMED, or 0 */
  char name[16];  /**< The user description of the characteristics (descriptor 0x2901), NUL-ended; "" = none */
} provider_ble_cfg_t;

_Static_assert(sizeof(provider_ble_cfg_t) == 23u, "provider_ble CREATE cfg");
