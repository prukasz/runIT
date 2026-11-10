#pragma once

#include "errors.h"
#include "sys_ble_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BLE peripheral (NimBLE) with a runtime GATT table of 16-bit
 * services and characteristics. The runIT service (0xFFE0) carries the router
 * link (sys_ble_link.h); user services can be added at runtime, e.g. a
 * characteristic a VM program streams over.
 *
 * Characteristic data paths:
 *   TX  sys_ble_char_send() queues one item; the BLE task sends it as a
 *       notification / indication once the client subscribed.
 *   RX  peer writes go either to the RX buffer (poll with
 *       sys_ble_char_rx_dequeue) or, with an RX handler, straight to it.
 * A framed characteristic carries frames longer than the MTU: each packet
 * starts with SYS_BLE_FRAG_* header byte (sys_ble_link.c reassembles).
 */

//#ref-defines ble_runit_uuid @alias runIT GATT UUIDs @prefix SYS_BLE_
#define SYS_BLE_SVC_RUNIT        0xFFE0  //@alias runIT service @description The primary service that carries the router link and the streams
#define SYS_BLE_CHR_RUNIT_TX     0xFFE1  //@alias Answers and telemetry @description Notify, framed: router answers, the telemetry and events streams
#define SYS_BLE_CHR_RUNIT_RX     0xFFE2  //@alias Requests @description Write, framed: router requests
#define SYS_BLE_CHR_RUNIT_LOGS   0xFFE3  //@alias Logs and errors @description Notify, framed: the logs and errors streams
#define SYS_BLE_CHT_RUNIT_STATUS 0xFFE4  //@alias Status @description Notify, unframed: created, nothing sends on it yet

/* Fragment header of a framed characteristic (first byte of every packet).
 *   bit 7 last, bit 6 first, bits 0-5 packet counter (mod 64, 0 at first).
 * A frame that fits one packet has 0xC0. */
//#ref-defines ble_frag @alias Fragment header @prefix SYS_BLE_FRAG_
#define SYS_BLE_FRAG_LAST       0x80u  //@alias Last packet @description Bit 7: the last packet of a frame
#define SYS_BLE_FRAG_FIRST      0x40u  //@alias First packet @description Bit 6: the first packet of a frame; the counter restarts at 0
#define SYS_BLE_FRAG_COUNT_MASK 0x3Fu  //@alias Packet counter mask @description Bits 0-5: the packet counter, modulo 64

/* ATT notification header (opcode + handle): payload = MTU - 3. */
#define SYS_BLE_ATT_HDR_LEN 3u

/** Bytes a no-split ring buffer keeps in front of each item. */
#define SYS_BLE_BUFF_ITEM_HDR 8u

/**
 * @brief Buffer size (bytes) holding `count` items of up to `item_max` bytes.
 * A no-split ring buffer takes one item of at most about half its size, so a
 * buffer sized in plain bytes holds items of only ~half that.
 * @usage .tx_buffer_size = SYS_BLE_BUFF_SIZE_FOR(CMD_FRAME_MAX, 4)
 */
#define SYS_BLE_BUFF_SIZE_FOR(item_max, count) \
  (((((size_t)(item_max) + 3u) & ~(size_t)3u) + SYS_BLE_BUFF_ITEM_HDR) * ((count) < 2u ? 2u : (size_t)(count)))

/** @brief A GATT service */
typedef struct {
  uint16_t uuid;    /**< 16-bit UUID */
  bool is_primary;  /**< Primary service */
} sys_ble_svc_cfg_t;

/** @brief A GATT characteristic */
typedef struct {
  uint16_t uuid;          /**< 16-bit UUID */
  bool is_write;          /**< The peer can write it */
  bool is_indicate;       /**< TX as indications (acknowledged) */
  bool is_notify;         /**< TX as notifications */
  bool is_framed;         /**< Fragment header on every packet (SYS_BLE_FRAG_*) */
  size_t tx_buffer_size;  /**< 0 = no TX */
  size_t rx_buffer_size;  /**< 0 = no RX buffer (writes need an RX handler then) */
  const char* desc;       /**< Optional user description (0x2901), copied */
} sys_ble_char_cfg_t;

/** @brief The link's state */
typedef struct {
  bool is_connected;         /**< A client is connected */
  uint16_t mtu;              /**< The link's MTU */
  uint32_t rx_overflow_count;  /**< Peer writes dropped for a full RX buffer */
} sys_ble_status_t;

/* ----------------------------------------------------------------------------
 * GATT table (staged; sys_ble_start / sys_ble_apply put it live)
 * ------------------------------------------------------------------------- */

/**
 * @brief Adds a service.
 * @param[in] cfg UUID and whether it is primary
 * @return ERR_BLE_UUID_TAKEN if a service has this UUID
 */
MUST_USE err_h sys_ble_service_create(const sys_ble_svc_cfg_t* cfg);

/**
 * @brief Removes a service (live: at the next apply).
 * @param[in] svc_uuid The service's UUID
 * @return ERR_BLE_SVC_LOCKED when locked
 */
MUST_USE err_h sys_ble_service_remove(uint16_t svc_uuid);

/**
 * @brief Locks a service: no characteristic changes, no removal. A change to
 * a live service rebuilds it and drops the client's subscriptions, so the
 * service of the command link is locked.
 * @param[in] svc_uuid The service's UUID
 * @return The error of the lock
 */
MUST_USE err_h sys_ble_service_lock(uint16_t svc_uuid);

/**
 * @brief Adds a characteristic to a service. Characteristic UUIDs are unique in the table; a service may share one.
 * @param[in] svc_uuid The service's UUID
 * @param[in] cfg      UUID, properties and buffers
 * @return ERR_BLE_UUID_TAKEN for a UUID in the table, ERR_BLE_SVC_LOCKED when the service is locked
 */
MUST_USE err_h sys_ble_char_create(uint16_t svc_uuid, const sys_ble_char_cfg_t* cfg);

/**
 * @brief Removes a characteristic (live service: at the next apply).
 * @param[in] svc_uuid  The service's UUID
 * @param[in] char_uuid The characteristic's UUID
 * @return ERR_BLE_SVC_LOCKED when the service is locked
 */
MUST_USE err_h sys_ble_char_remove(uint16_t svc_uuid, uint16_t char_uuid);

/**
 * @brief First call: builds the table and starts NimBLE, advertising and the
 * BLE task. Later calls: applies staged changes right away.
 * @return The error of the NimBLE start or of the apply
 */
MUST_USE err_h sys_ble_start(void);

/**
 * @brief Applies staged changes. With a client connected the BLE task does it
 * CONFIG_SYS_BLE_APPLY_DELAY_MS later, so the command asking for it is
 * answered first: a client may drop all GATT objects on the Service Changed
 * indication (Windows does) and reconnects.
 * @return The error of the apply
 */
MUST_USE err_h sys_ble_apply(void);

/* ----------------------------------------------------------------------------
 * Data
 * ------------------------------------------------------------------------- */

/**
 * @brief Queues one item for TX. Not connected: dropped, NULL. A framed
 * characteristic takes items up to its buffer limit (split into packets by the
 * BLE task); an unframed one up to MTU - 3.
 * @param[in] char_uuid The characteristic's UUID
 * @param[in] data      The item
 * @param[in] len       Item size in bytes
 * @param[in] wait_ms   How long to wait for buffer space (0 = fail at once)
 * @return The error of the queueing
 */
MUST_USE err_h sys_ble_char_send(uint16_t char_uuid, const uint8_t* data, size_t len, uint32_t wait_ms);

/**
 * @brief Largest item sys_ble_char_send() takes on this characteristic now:
 * its TX buffer limit, and MTU - 3 when unframed (link MTU only while
 * connected). 0 for an unknown characteristic or one without TX.
 * @param[in] char_uuid The characteristic's UUID
 * @return Bytes
 */
size_t sys_ble_char_max_payload(uint16_t char_uuid);

/**
 * @brief Pops one peer write from the RX buffer. Empty: NULL and *out_len 0.
 * An item longer than `max_len` is dropped: ERR_BLE_TOO_LONG.
 * @param[in]  char_uuid The characteristic's UUID
 * @param[out] buf       Buffer for the item
 * @param[in]  max_len   Size of buf
 * @param[out] out_len   Item size in bytes, 0 when empty
 * @return ERR_BLE_TOO_LONG, or the error of the read
 */
MUST_USE err_h sys_ble_char_rx_dequeue(uint16_t char_uuid, uint8_t* buf, size_t max_len, size_t* out_len);

/**
 * @brief Direct RX: each peer write is passed to `fn` on the BLE host task
 * (outside the registry lock; must not block). For a characteristic without
 * an RX buffer. One handler per characteristic.
 * @param ctx  The handler's context
 * @param data The peer's write, borrowed
 * @param len  Its size in bytes
 */
typedef void (*sys_ble_rx_f)(void* ctx, const uint8_t* data, size_t len);

/**
 * @brief Sets the direct RX handler of a characteristic.
 * @param[in] char_uuid The characteristic's UUID
 * @param[in] fn        The handler
 * @param[in] ctx       Passed to fn
 * @return The error of the set
 */
MUST_USE err_h sys_ble_char_set_rx_handler(uint16_t char_uuid, sys_ble_rx_f fn, void* ctx);

/**
 * @brief Feeds bytes to a characteristic's RX path as if the peer wrote them (tests).
 * @param[in] char_uuid The characteristic's UUID
 * @param[in] data      The bytes
 * @param[in] len       Number of bytes
 * @return The error of the RX path
 */
MUST_USE err_h sys_ble_char_rx_inject(uint16_t char_uuid, const uint8_t* data, size_t len);

/* ----------------------------------------------------------------------------
 * Connection
 * ------------------------------------------------------------------------- */

/**
 * @brief Called on connect (reason 0) and disconnect (NimBLE reason), on the
 * BLE host task; must not block. Up to CONFIG_SYS_BLE_MAX_CONN_LISTENERS,
 * registered at boot.
 * @param connected true on connect
 * @param reason    0 on connect, NimBLE's reason on disconnect
 */
typedef void (*sys_ble_conn_f)(bool connected, int32_t reason);

/**
 * @brief Adds a connection listener.
 * @param[in] fn The listener
 * @return The error when all CONFIG_SYS_BLE_MAX_CONN_LISTENERS are used
 */
MUST_USE err_h sys_ble_add_conn_listener(sys_ble_conn_f fn);

/**
 * @brief Reads the link's state
 * @param[out] out Connected, MTU and the RX overflow count
 * @return The error of the read
 */
MUST_USE err_h sys_ble_get_status(sys_ble_status_t* out);

/**
 * @brief Walks the GATT table as it will be after the next apply: each service (`chr` NULL), then each of its
 * characteristics, in table order. A service or characteristic removed but not applied yet is left out. Runs under
 * the registry lock: `fn` must not block or call sys_ble. An error from `fn` stops the walk and is returned.
 * @param ctx    The visitor's context
 * @param svc    The service
 * @param locked The service is locked
 * @param chr    The characteristic; NULL for the service itself
 */
typedef err_h (*sys_ble_visit_f)(void* ctx, const sys_ble_svc_cfg_t* svc, bool locked, const sys_ble_char_cfg_t* chr);

/**
 * @brief Walks the GATT table as it will be after the next apply.
 * @param[in] fn  Called for every service and characteristic
 * @param[in] ctx Passed to fn
 * @return The error of fn that stopped the walk
 */
MUST_USE err_h sys_ble_table_visit(sys_ble_visit_f fn, void* ctx);

#ifdef __cplusplus
}
#endif
