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
 * one hook per characteristic hears both its writes and the link's state. Nothing here knows providers or the runIT
 * service: provider_ble makes the characteristics of a provider (SYS_BLE.MD), the board's composition the first ones.
 */

/* Flags of a service (sys_ble_service_create, LIST) */
#define SYS_BLE_SVC_PRIMARY (1u << 0)  /**< A primary service */
#define SYS_BLE_SVC_LOCKED  (1u << 1)  /**< LIST only: the board's own, no change, no removal */

/* Flags of a characteristic (sys_ble_char_create, LIST) */
#define SYS_BLE_CHR_WRITE    (1u << 0)  /**< The peer writes it */
#define SYS_BLE_CHR_INDICATE (1u << 1)  /**< Out as indications */
#define SYS_BLE_CHR_NOTIFY   (1u << 2)  /**< Out as notifications */
#define SYS_BLE_CHR_FRAMED   (1u << 3)  /**< Fragment header on every packet (SYS_BLE_FRAG_*) */

/** The bytes of a TX buffer that holds `items` frames of CMD_FRAME_MAX (a no-split ring buffer keeps 8 B in front of each). */
#define SYS_BLE_TX_BYTES(items) ((CMD_FRAME_MAX + 8u) * (items))

/* Fragment header of a framed characteristic: the first byte of every packet. A frame in one packet has 0xC0. */
//#ref-defines ble_frag @alias Fragment header @prefix SYS_BLE_FRAG_
#define SYS_BLE_FRAG_LAST       0x80u  //@alias Last packet @description Bit 7: the last packet of a frame
#define SYS_BLE_FRAG_FIRST      0x40u  //@alias First packet @description Bit 6: the first packet of a frame; the counter restarts at 0
#define SYS_BLE_FRAG_COUNT_MASK 0x3Fu  //@alias Packet counter mask @description Bits 0-5: the packet counter, modulo 64

/**
 * @brief A characteristic's hook, on the BLE host task (the link's state also on the task that changed it), one hook
 * call at a time; must not block (the host waits for it). `data` != NULL: one write of the peer (a whole frame on a
 * framed characteristic), borrowed. `data` == NULL: the link's state, `len` 1 up (a client is connected), 0 down (it
 * left, or the characteristic is removed: the hook is let go). Once let go, a hook is never called again.
 */
typedef void (*sys_ble_rx_f)(void* ctx, const uint8_t* data, uint32_t len);

/**
 * @brief Starts NimBLE with the table as staged so far (services and characteristics made with the calls below, by
 * the providers), advertising `adv_service`, and the BLE task. Once, at boot, after the board's first providers.
 * @param[in] adv_service The service UUID the advertisement carries: a client filters by it
 * @return ERR_BLE_STACK, ERR_NO_MEM
 */
MUST_USE err_h sys_ble_start(uint16_t adv_service);

/**
 * @brief The table's calls, the providers' (nothing else changes the table): staged until sys_ble_apply puts them live. A characteristic
 * is in one service and its UUID is unique in the table.
 * @return ERR_BLE_UUID_TAKEN, ERR_BLE_NOT_FOUND, ERR_BLE_SVC_LOCKED, ERR_NO_MEM, ERR_BAD_ARG
 */
MUST_USE err_h sys_ble_service_create(uint16_t uuid, bool primary);
MUST_USE err_h sys_ble_service_remove(uint16_t uuid);
/** @brief The board's own service: no change, no removal from now on (its characteristics must be made first). */
MUST_USE err_h sys_ble_service_lock(uint16_t uuid);
/** @param tx Its TX buffer in bytes (SYS_BLE_TX_BYTES), 0 = none; needs NOTIFY or INDICATE. `name`: its user description, or NULL. */
MUST_USE err_h sys_ble_char_create(uint16_t svc, uint16_t uuid, uint8_t flags, uint16_t tx, const char* name);
MUST_USE err_h sys_ble_char_remove(uint16_t uuid);
/** @brief Puts the staged changes live: at once, or CONFIG_SYS_BLE_APPLY_DELAY_MS later on the BLE task with a client. */
void sys_ble_apply(void);

/**
 * @brief Takes the hook of a characteristic, or lets it go (`cb` NULL; the hook's `ctx` only, a gone characteristic is
 * NULL; it waits for the call of a hook in flight). A taken hook hears the link's state at once. Never from a hook.
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
