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
 * The UART contract, UART: 0x13xx, where 0x13 is the contract and xx its op. A port is a device: the ESP32-S3's own
 * UART controller (device_esp32s3_uart: any UART port the board or project creates), and so a UART bridge on I2C or
 * SPI. As many as the project creates, each with its device ID. A driver names its port solely by that device ID (a u8
 * of its create struct) and reaches it only through the ops below, never through its implementation. Neither hardware
 * controller numbers (belonging in the driver's CREATE config) nor sys_data_provider concepts belong in this contract:
 * ports stay completely isolated from sys_data_provider.
 *
 * A port has one reader at a time, and it is one of these:
 *  - a device that SUBSCRIBEd: each message is delivered to its own op (sys_device_deliver), on the port's RX task;
 *  - an external C consumer attached via RX_HOOK (LOCAL, e.g. a provider adapter bridging a port to sys_session or an
 *    IN stream): each message goes to its callback on the port's RX task;
 *  - nobody: the messages wait in the port's buffer for RECEIVE or TRANSACT.
 * Access rules:
 *  - RECEIVE, FLUSH, SUBSCRIBE, TRANSACT and binding RX_HOOK (`cb` != NULL) are refused (ERR_UART_IN_USE{port, owner})
 *    whenever the port has a reader (`owner` is the subscriber's device ID, or SYS_DEVICE_ID_NONE for RX_HOOK).
 *    Unsubscribing (SUBSCRIBE with op 0) requires `caller == subscriber`; unbinding RX_HOOK (`cb` == NULL) requires
 *    `ctx` matching the bound hook's `ctx` (ERR_UART_IN_USE otherwise).
 *  - SEND and SEND_FRAME are allowed when: nobody reads the port; `caller == subscriber`; RX_HOOK has `allow_send`
 *    (RAW / RAW_LINE, e.g. an IN stream); or SEND_FRAME carries `token == hook.ctx` (the bound adapter sending on
 *    FRAMED). Generic SEND is refused (ERR_UART_IN_USE) for non-subscribers when a device subscribed, and when RX_HOOK
 *    has `allow_send` false (never mix raw bytes with a session's COBS frames); DIRECT is handled outside the contract
 *    in sys_uart.
 * A message is a FRAMED port's frame (its CRC checked), a RAW port's bytes up to a pause in the line, a RAW_LINE port's
 * line up to its line feed; at most CMD_FRAME_MAX bytes locally (CMD_DATA_MAX via remote commands).
 *
 * Protocols needing direct hardware timing (the bootloader flasher on the board's link) bypass device ops and use
 * DIRECT mode in sys_uart directly (sys_uart_link_direct, sys_data_provider_open, sys_uart_write / sys_uart_read).
 *
 * INFO       - the port: its mode, rate, parity, stop bits
 * BAUD       - its rate, read and set (SYS_UART_BAUD_MIN..MAX)
 * SEND       - bytes onto the line (TAIL, 1..CMD_DATA_MAX - head): CMD_SEND_URGENT (priority queue written first),
 *              CMD_SEND_SPARE (refuses ERR_UART_BUSY past half queue), or CMD_SEND_NOWAIT (never wait, same as
 *              wait_ms = 0). Waits for room up to `wait_ms` (0 = NOWAIT), NOLOCK
 * SEND_FRAME - LOCAL flat frame (`cmd_in_t`, 1..CMD_FRAME_MAX) with the same CMD_SEND_* flags, `wait_ms` and
 *              optional `token` matching RX_HOOK `ctx`, NOLOCK
 * RECEIVE    - the next message in the buffer, waiting up to `wait_ms`: answers the raw message bytes directly via
 *              cmd_reply (ERR_CMD_OUT_FULL if larger than out->cap; empty when none came). NOLOCK
 * FLUSH      - the buffer emptied
 * SUBSCRIBE  - the caller (a device) the port's reader: each message to its own op `op` (LOCAL | TAIL | ALWAYS, best
 *              NOLOCK) as sys_uart_rx_t and the message; `op` 0 lets it go. Local
 * TRANSACT   - atomic FLUSH, SEND and RECEIVE in one call (TAIL: head + TX bytes; answers the raw reply bytes via
 *              cmd_reply). NOLOCK
 * RX_HOOK    - LOCAL non-device reader (`cb`, `ctx`, `allow_send`) for transport adapters; `cb` NULL releases it
 *
 * Every write op is ALWAYS (and reads always run): a port has no outputs to make safe, so a suspended port (the safe
 * state) keeps carrying the link's frames and its drivers' suspend hooks. SUBSCRIBE, SEND_FRAME and RX_HOOK are LOCAL:
 * only firmware C reaches them, never a command or a VM program.
 * Event: UART_EV_OVERFLOW (`dropped` u16, SYS_EVENT_NOTIFY) published on real RX buffer/FIFO/queue overflow (never for
 * corrupted FRAMED candidates).
 * Most ops are called directly via SYS_DEVICE_SET / SYS_DEVICE_GET / SYS_DEVICE_WRITE (or sys_device_call for
 * variable-length RECEIVE). Only SUBSCRIBE has a helper below (sys_uart_port_subscribe), because it registers the
 * subscription as a registry resource of the owner. A non-subscribing driver should call sys_device_use(id, port) in
 * its create() so the registry refuses REMOVE on the port while it exists.
 * @usage ERR_TRY(sys_uart_port_subscribe(id, &c->port, cfg->uart, MY_OP_RX));
 *        ERR_TRY(SYS_DEVICE_SET(cfg->uart, CONTRACT_UART, OP_SEND_FRAME, .frame = {buf, len}, .wait_ms = 20));
 *        sys_uart_info_t info; ERR_TRY(SYS_DEVICE_GET(cfg->uart, CONTRACT_UART, OP_INFO, &info));
 */

/* ----------------------------------------------------------------------------
 * Modes, parity and limits (wire format: never renumber; published to the app)
 * ------------------------------------------------------------------------- */

/** @brief What the port carries. A device port is FRAMED, RAW or RAW_LINE; a lent board link (sys_uart) is DIRECT. */
//#ref-enum @alias UART mode
typedef enum {
  SYS_UART_MODE_FRAMED = 0,    //@alias Framed @description COBS and CRC16 frames: a session can attach it (the link to another board)
  SYS_UART_MODE_RAW = 1,       //@alias Raw @description The bytes as they are; a message is what arrives before a pause in the line
  SYS_UART_MODE_RAW_LINE = 2,  //@alias Raw lines @description The bytes as they are; a message ends at a line feed (a GPS's sentences)
  SYS_UART_MODE_DIRECT = 3,    //@alias Direct @description No tasks of its own: its owner writes and reads on its own task, for protocols with exact timing (the flasher)
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

/* Send flags in sys_uart_send_t and sys_uart_send_frame_t use shared CMD_SEND_* from cmd.h (0 = normal TX queue). */

/* ----------------------------------------------------------------------------
 * Wire frames and op table
 * ------------------------------------------------------------------------- */

/** @brief INFO answer */
typedef struct __attribute__((packed)) {
  uint8_t mode;       /**< sys_uart_mode_e: FRAMED, RAW, RAW_LINE; DIRECT while lent by sys_uart */
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

/** @brief SEND_FRAME args (`LOCAL` C only): one flat frame up to CMD_FRAME_MAX without an intermediate copy */
typedef struct {
  cmd_in_t frame;     /**< Frame bytes (`frame.len`: 1..CMD_FRAME_MAX) */
  uint16_t wait_ms;   /**< For room in the port's queue, at most SYS_UART_WAIT_MAX_MS; 0: none (NOWAIT) */
  uint8_t flags;      /**< Bitmask of CMD_SEND_* (URGENT, SPARE, NOWAIT); 0: normal TX queue */
  const void* token;  /**< Matches `rx_hook.ctx` when sending on a FRAMED port held by RX_HOOK; NULL otherwise */
} sys_uart_send_frame_t;

/**
 * @brief Non-device RX callback (`OP_RX_HOOK`), run on the port's RX task (never an ISR). Copy and return; send only
 *        with `wait_ms` 0 (NOWAIT).
 * @param ctx  Caller context from `sys_uart_rx_hook_t`
 * @param data One whole message; NULL when the port went up or down (e.g. DIRECT mode or port removal)
 * @param len  Bytes in `data`; with `data` NULL: 1 = up, 0 = down
 */
typedef void (*sys_uart_rx_cb_f)(void* ctx, const uint8_t* data, uint32_t len);

/** @brief RX_HOOK args (`LOCAL` C only): binds or releases the port's non-device reader */
typedef struct {
  sys_uart_rx_cb_f cb;  /**< Reader callback; NULL releases the hook (only when `ctx` matches) */
  void* ctx;            /**< Context passed to `cb` and matched as `token` on release / FRAMED SEND_FRAME */
  bool allow_send;      /**< True for RAW / RAW_LINE (external SEND allowed, e.g. IN stream); must be false on FRAMED */
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

/* Events of the UART contract (source = port device ID, channel = 0). Published on real RX buffer/FIFO/queue overflow
 * with rate throttling; corrupted FRAMED candidates (bad CRC / boot text) are ignored silently and never emit OVERFLOW. */
#define UART_EVENTS(X, P) \
  X(P, OVERFLOW, 0x01, SYS_EVENT_NOTIFY, { uint16_t dropped; })

SYS_EVENTS_DECLARE(UART)

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
