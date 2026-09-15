#pragma once
#include <stdint.h>
#include "sys_ble.h"
#include "sys_interface.h"

/**
 * @brief Adapt a BLE characteristic dequeue operation to an interface source.
 *
 * @param ctx BLE characteristic UUID encoded as a pointer-sized value.
 * @param buf Destination frame buffer.
 * @param max_len Capacity of @p buf.
 * @param out_len Receives the dequeued frame length, or zero when empty.
 * @return NULL on success, otherwise an error handle from the BLE layer.
 */
static inline err_h sys_interface_config_ble_rx_dequeue(void* ctx, uint8_t* buf, size_t max_len, size_t* out_len) {
  return sys_ble_char_rx_dequeue((uint16_t)(uintptr_t)ctx, buf, max_len, out_len);
}

/**
 * @brief Attach a BLE characteristic's RX buffer to the interface router.
 *
 * Header-only transport configuration. Other transports can provide an
 * equivalent adapter and call sys_interface_register_rx_source().
 *
 * @param char_uuid BLE characteristic UUID to register as an RX source.
 * @param max_frame_len Maximum accepted frame length.
 * @return NULL on success, otherwise an error from BLE validation or source registration.
 */
static inline err_h sys_interface_bind_ble_rx(uint16_t char_uuid, size_t max_frame_len) {
  err_h err = sys_ble_char_check_rx_enabled(char_uuid);
  if (SE_IS_ERR(err)) return err;
  return sys_interface_register_rx_source(sys_interface_config_ble_rx_dequeue, (void*)(uintptr_t)char_uuid, max_frame_len, "ble_rx");
}
