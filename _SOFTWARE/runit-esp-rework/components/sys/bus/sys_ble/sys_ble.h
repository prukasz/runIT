#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cmd.h"
#include "errors.h"
#include "sys_ble_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file sys_ble.h
 * @brief The board's BLE peripheral (NimBLE): a GATT table of 16-bit services and characteristics that changes at run
 * time (its router, MODULE_ID_BLE). A characteristic is a way in (the peer writes it) and out (notify / indicate);
 * one hook per characteristic hears both its writes and the link's state. Nothing here knows providers: provider_ble
 * makes one of a characteristic (SYS_BLE.MD).
 */

//#ref-defines ble_runit_uuid @alias runIT GATT UUIDs @prefix SYS_BLE_
#define SYS_BLE_SVC_RUNIT    0xFFE0  //@alias runIT service @description The board's own service: the client's session
#define SYS_BLE_CHR_RUNIT_TX 0xFFE1  //@alias Session out @description Notify, framed: the session's frames to the client
#define SYS_BLE_CHR_RUNIT_RX 0xFFE2  //@alias Session in @description Write, framed: the session's frames from the client

/* Fragment header of a framed characteristic: the first byte of every packet. A frame in one packet has 0xC0. */
//#ref-defines ble_frag @alias Fragment header @prefix SYS_BLE_FRAG_
#define SYS_BLE_FRAG_LAST       0x80u  //@alias Last packet @description Bit 7: the last packet of a frame
#define SYS_BLE_FRAG_FIRST      0x40u  //@alias First packet @description Bit 6: the first packet of a frame; the counter restarts at 0
#define SYS_BLE_FRAG_COUNT_MASK 0x3Fu  //@alias Packet counter mask @description Bits 0-5: the packet counter, modulo 64

/**
 * @brief A characteristic's hook, on the BLE host task; must not block. `data` != NULL: one write of the peer (a whole
 * frame on a framed characteristic), borrowed. `data` == NULL: the link's state, `len` 1 up (a client is connected),
 * 0 down (it left, or the characteristic is removed: the hook is let go).
 */
typedef void (*sys_ble_rx_f)(void* ctx, const uint8_t* data, uint32_t len);

/**
 * @brief Creates the runIT service (locked: TX, RX) and starts NimBLE, advertising and the BLE task. Once, at boot.
 * @return ERR_BLE_STACK, ERR_BLE_NO_MEM
 */
MUST_USE err_h sys_ble_init(void);

/**
 * @brief Takes the hook of a characteristic, or lets it go (`cb` NULL; the hook's `ctx` only, a gone characteristic is
 * NULL). A taken hook hears the link's state at once.
 * @param[in]  uuid   The characteristic
 * @param[in]  cb     The hook, NULL to let it go
 * @param[in]  ctx    Passed to cb
 * @param[out] framed Whether the characteristic is framed; may be NULL
 * @return ERR_BLE_NOT_FOUND, ERR_BLE_HOOK (another ctx holds it)
 */
MUST_USE err_h sys_ble_hook(uint16_t uuid, sys_ble_rx_f cb, void* ctx, bool* framed);

/**
 * @brief Queues one item for the client: a frame on a framed characteristic (split into packets by the BLE task), else
 * bytes sent as notifications of the MTU less 3. No client: dropped, NULL. Items keep their order (CMD_SEND_URGENT
 * goes in turn); CMD_SEND_SPARE is refused while the buffer is half full.
 * @param[in] uuid    The characteristic
 * @param[in] data    The item, 1..min(its buffer's largest item, CMD_FRAME_MAX) bytes
 * @param[in] flags   CMD_SEND_*
 * @param[in] wait_ms How long to wait for room
 * @return ERR_BLE_NOT_FOUND, ERR_BLE_NO_TX, ERR_BLE_TOO_LONG, ERR_BLE_TX_FULL
 */
MUST_USE err_h sys_ble_send(uint16_t uuid, cmd_in_t data, uint8_t flags, uint32_t wait_ms);

#ifdef __cplusplus
}
#endif
