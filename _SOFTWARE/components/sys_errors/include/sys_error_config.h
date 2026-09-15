#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sys_ble.h"
#include "sys_error.h"

/**
 * @file sys_error_config.h
 * @brief Transport bindings for the outbound error stream - the mirror image
 * of `sys_interface_config.h`, which binds a transport to the *inbound* one.
 *
 * Kept header-only and outside `sys_error.h` on purpose: this is the only
 * place where sys_errors names a concrete transport, so the generic API (and
 * the handler task) stays free of any BLE dependency.
 */

/**
 * @brief Adapt a BLE characteristic send to an error TX sink.
 *
 * @param ctx BLE characteristic UUID encoded as a pointer-sized value.
 * @param header TX slot header byte identifying the error stream.
 * @param data Encoded error packet.
 * @param len Length of @p data.
 * @return err_h NULL on success, otherwise an error handle from the BLE layer
 *               (dropped by the handler task, see se_tx_sink_f).
 */
static inline err_h sys_error_config_ble_tx_send(void* ctx, uint8_t header, const uint8_t* data, size_t len) {
  return sys_ble_char_send((uint16_t)(uintptr_t)ctx, header, data, len, true);
}

/**
 * @brief Send encoded error packets to a BLE characteristic's TX slot.
 *
 * Call it after the characteristic and its TX buffer exist
 * (`sys_ble_static_config()`); the slot header itself comes from
 * `sys_error_cfg_t.errors.tx_header`, so bind once and re-point the stream
 * with SE_configure() alone. Unlike sys_interface_bind_ble_rx() this returns
 * nothing - it is a slot assignment, and there is no TX counterpart to
 * sys_ble_char_check_rx_enabled() to validate against, so a wrong UUID
 * surfaces as a failed send (which the handler drops) rather than here.
 *
 * @param char_uuid BLE characteristic UUID to carry the error stream.
 */
static inline void SE_bind_ble_tx(uint16_t char_uuid) {
  SE_register_tx_sink(sys_error_config_ble_tx_send, (void*)(uintptr_t)char_uuid, "ble_tx");
}
