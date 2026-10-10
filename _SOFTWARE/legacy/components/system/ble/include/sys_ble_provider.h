#pragma once
#include <stdint.h>
#include "sys_error.h"

/**
 * @file sys_ble_provider.h
 * @brief BLE as a sys_data_connector transport provider.
 *
 * - **Endpoint** = 16-bit characteristic UUID (TX: a notify/indicate
 *   characteristic with a TX buffer; RX: a write characteristic with an RX buffer).
 * - **Framing:** one peer write is one frame and one notification is one
 *   frame, so the provider needs no de-framing. A frame never exceeds the
 *   negotiated ATT MTU - 3 (reported as the provider's frame limit).
 * - **Peer:** BLE keeps one connection, so the peer is always 0.
 *
 * @code
 * SE_TRY(sys_ble_provider_register(RUNIT_DATA_PROVIDER_BLE));
 * SE_TRY(sys_data_connector_bind_tx(SYS_DATA_CONNECTOR_LOGS, RUNIT_DATA_PROVIDER_BLE, SYS_BLE_CHR_RUNIT_LOGS));
 * @endcode
 */

/**
 * @brief Register the BLE provider with sys_data_connector under @p provider_id.
 *
 * @return err_h NULL on success, or sys_data_connector_register_provider()'s error.
 */
SE_MUST_USE err_h sys_ble_provider_register(uint8_t provider_id);
