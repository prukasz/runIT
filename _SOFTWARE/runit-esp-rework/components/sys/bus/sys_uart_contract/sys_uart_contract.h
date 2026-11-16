#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cmd.h"
#include "errors.h"
#include "sys_device.h"
#include "sys_device_driver.h"  // sys_device_res_t: a subscription is a resource of its device
#include "sys_event.h"          // UART_EV_OVERFLOW
#include "sys_uart_errors.h"    // MODULE_ID_UART (0x13), ERR_UART_*

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The UART contract, UART: 0x13xx (SYS_UART_CONTRACT.MD). A port is a device (device_esp32s3_uart, a bridge on I2C or
 * SPI), created by the project with any configuration and reached only by its device ID: no controller number, no
 * provider here. A port has one reader at a time: a device that SUBSCRIBEd (each message to its own op), a C consumer
 * on RX_HOOK (a provider adapter: a way to another board), or a RECEIVE / TRANSACT under way; with none, messages
 * wait for RECEIVE. RECEIVE, FLUSH, TRANSACT, SUBSCRIBE and RX_HOOK are refused while another reader holds the port
 * (ERR_UART_IN_USE); a reader lets go only of what it holds. SEND is refused on a port held by a subscriber, but for
 * the subscriber, and on a FRAMED port held by a hook (raw bytes would break its frames). SEND_FRAME is C's, never
 * refused. A message is a FRAMED frame (its CRC checked), a RAW port's bytes up to a pause, a RAW_LINE port's line.
 * Every write op is ALWAYS: the safe state keeps the sessions going. SUBSCRIBE, SEND_FRAME and RX_HOOK are LOCAL.
 * @usage ERR_TRY(sys_uart_port_subscribe(id, &c->port, cfg->uart, MY_OP_RX));
 *        ERR_TRY(SYS_DEVICE_SET(cfg->uart, CONTRACT_UART, OP_SEND_FRAME, .frame = {buf, len}, .wait_ms = 20));
 */

/* ----------------------------------------------------------------------------
 * Modes, parity and limits (wire format: never renumber; published to the app)
 * ------------------------------------------------------------------------- */

/** @brief What the port carries. */
//#ref-enum @alias UART mode
typedef enum {
  SYS_UART_MODE_FRAMED = 0,    //@alias Framed @description COBS and CRC16 frames: a session can attach it (a way to another board)
  SYS_UART_MODE_RAW = 1,       //@alias Raw @description The bytes as they are; a message is what arrives before a pause in the line
  SYS_UART_MODE_RAW_LINE = 2,  //@alias Raw lines @description The bytes as they are; a message ends at a line feed (a GPS's sentences)
} sys_uart_mode_e;

/** @brief The parity bit. */
//#ref-enum @alias UART parity
typedef enum {
  SYS_UART_PARITY_NONE = 0,  //@alias None @description No parity bit
  SYS_UART_PARITY_EVEN = 1,  //@alias Even @description Even parity
  SYS_UART_PARITY_ODD = 2,   //@alias Odd @description Odd parity
} sys_uart_parity_e;

/* How long SEND, RECEIVE and TRANSACT wait: the caller's task (a router task for a command) waits with them. */
//#ref-defines uart_wait @alias UART wait @prefix SYS_UART_WAIT_
#define SYS_UART_WAIT_MAX_MS 1000u  //@alias Longest wait @description The longest SEND waits for room and RECEIVE / TRANSACT for a message (ms)

/* The rates a port is set to (OP_BAUD and sys_uart_set_baud): any rate between, not a list. */
//#ref-defines uart_baud @alias UART baud rate @prefix SYS_UART_BAUD_
#define SYS_UART_BAUD_MIN 1200u     //@alias Lowest @description The lowest baud rate
#define SYS_UART_BAUD_MAX 5000000u  //@alias Highest @description The highest baud rate (a short cable)

/* ----------------------------------------------------------------------------
 * Wire frames and op table
 * ------------------------------------------------------------------------- */

/** @brief INFO answer */
typedef struct __attribute__((packed)) {
  uint8_t mode;       /**< sys_uart_mode_e */
  uint32_t baud;      /**< The rate now */
  uint8_t parity;     /**< sys_uart_parity_e */
  uint8_t stop_bits;  /**< 1 or 2; 8 data bits */
} sys_uart_info_t;

/** @brief SEND args: the bytes follow (TAIL), at least 1 */
typedef struct __attribute__((packed)) {
  uint16_t wait_ms;  /**< For room in the port's queue, at most SYS_UART_WAIT_MAX_MS; 0: none (NOWAIT) */
  uint8_t flags;     /**< Bitmask of CMD_SEND_* (URGENT, SPARE, NOWAIT); 0: normal TX queue */
} sys_uart_send_t;

/** @brief RECEIVE key */
typedef struct __attribute__((packed)) {
  uint16_t wait_ms;  /**< For a whole message, at most SYS_UART_WAIT_MAX_MS; 0: only one waiting now */
} sys_uart_receive_t;

/** @brief SUBSCRIBE args */
typedef struct __attribute__((packed)) {
  uint16_t op;  /**< The caller's own op that takes each message (SYS_DEVICE_OWN_OP); 0: no subscriber */
} sys_uart_subscribe_t;

/** @brief What a subscriber's op gets: this head, then the message (its tail) */
typedef struct __attribute__((packed)) {
  uint8_t port;  /**< The port's device ID */
} sys_uart_rx_t;

/** @brief TRANSACT args: head then TX bytes (TAIL), at least 1; answers the reply bytes directly */
typedef struct __attribute__((packed)) {
  uint16_t wait_ms;  /**< For TX room and the reply, at most SYS_UART_WAIT_MAX_MS; 0: poll once after TX */
} sys_uart_transact_t;

/** @brief SEND_FRAME args (LOCAL): one flat frame, copied once into the port's queue */
typedef struct {
  cmd_in_t frame;    /**< 1..CMD_FRAME_MAX bytes */
  uint16_t wait_ms;  /**< For room in the port's queue, at most SYS_UART_WAIT_MAX_MS; 0: none (NOWAIT) */
  uint8_t flags;     /**< CMD_SEND_* (URGENT, SPARE, NOWAIT); 0: the normal queue */
} sys_uart_send_frame_t;

/**
 * @brief An RX_HOOK reader, on the port's RX task: copy and return, send only NOWAIT.
 * @param data One whole message; NULL once, when the port is removed (`len` 0)
 */
typedef void (*sys_uart_rx_cb_f)(void* ctx, const uint8_t* data, uint32_t len);

/** @brief RX_HOOK args (LOCAL): `cb` the reader, NULL lets go of the hook whose `ctx` this is */
typedef struct {
  sys_uart_rx_cb_f cb;
  void* ctx;
} sys_uart_rx_hook_t;

/** @brief The contract, once: X(P, OP_NAME, op number, key_t, value_t, flags). Sides: INFO R, BAUD R/W, SEND W,
 *         RECEIVE R, FLUSH W, SUBSCRIBE W, TRANSACT W, SEND_FRAME W (LOCAL), RX_HOOK W (LOCAL). RECEIVE and TRANSACT
 *         answer the raw message bytes directly via cmd_reply. */
#define CONTRACT_UART_OPERATIONS(X, P)                                                                            \
  X(P, OP_INFO,       0x00, cmd_none_t,         sys_uart_info_t,       0)                                         \
  X(P, OP_BAUD,       0x01, cmd_none_t,         uint32_t,              SYS_DEVICE_OP_ALWAYS)                      \
  X(P, OP_SEND,       0x02, cmd_none_t,         sys_uart_send_t,                                                  \
    SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_TAIL | SYS_DEVICE_OP_NOLOCK)                                           \
  X(P, OP_RECEIVE,    0x03, sys_uart_receive_t, cmd_none_t,            SYS_DEVICE_OP_NOLOCK)                      \
  X(P, OP_FLUSH,      0x04, cmd_none_t,         cmd_none_t,            SYS_DEVICE_OP_ALWAYS)                      \
  X(P, OP_SUBSCRIBE,  0x05, cmd_none_t,         sys_uart_subscribe_t,  SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_LOCAL)\
  X(P, OP_TRANSACT,   0x06, cmd_none_t,         sys_uart_transact_t,                                              \
    SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_TAIL | SYS_DEVICE_OP_NOLOCK)                                           \
  X(P, OP_SEND_FRAME, 0x07, cmd_none_t,         sys_uart_send_frame_t,                                            \
    SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_LOCAL | SYS_DEVICE_OP_NOLOCK)                                          \
  X(P, OP_RX_HOOK,    0x08, cmd_none_t,         sys_uart_rx_hook_t,    SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_LOCAL)

SYS_DEVICE_CONTRACT_DECLARE(CONTRACT_UART, UART, 0x13)

/* Events of the UART contract (source = port device ID, channel = 0): OVERFLOW on each overflow of the line's RX FIFO
 * or driver buffer (`dropped` 1: one overflow, bytes lost). A FRAMED candidate failing its CRC is no event. */
#define UART_EVENTS(X, P) \
  X(P, OVERFLOW, 0x01, SYS_EVENT_NOTIFY, { uint16_t dropped; })

SYS_EVENTS_DECLARE(UART, DEVICE)

/* ----------------------------------------------------------------------------
 * The client: a driver subscribing to a port
 * ------------------------------------------------------------------------- */

/** @brief A driver's subscription to a port, embedded in its struct. Zeroed: none. */
typedef struct {
  sys_device_res_t res;  /**< The subscription as a resource of its device: the registry's */
  uint8_t port;          /**< The port's device ID */
  uint8_t subscribed;    /**< 1 after sys_uart_port_subscribe() */
} sys_uart_port_t;

/**
 * @brief Makes the owner the reader of port `port`: declares the use (the port has SUBSCRIBE, else ERR_DEV_UNKNOWN_OP),
 * then SUBSCRIBE to the owner's own op `op`. Called from the owner's create(). The subscription is a resource of the
 * owner: the registry lets it go when the owner is removed or its create() fails.
 * @param[in]  owner The owner's device ID
 * @param[out] sub   The subscription
 * @param[in]  port  The port's device ID
 * @param[in]  op    The owner's own op (SYS_DEVICE_OWN_OP): a write, LOCAL | TAIL | ALWAYS, its value sys_uart_rx_t
 * @return ERR_DEV_NOT_FOUND{port}, ERR_DEV_UNKNOWN_OP, or the port's SUBSCRIBE error (ERR_UART_IN_USE{port, owner})
 */
MUST_USE err_h sys_uart_port_subscribe(uint8_t owner, sys_uart_port_t* sub, uint8_t port, uint16_t op);

#ifdef __cplusplus
}
#endif
