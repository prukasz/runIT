#pragma once

#include "errors.h"
#include "sdkconfig.h"
#include "sys_data_provider.h"
#include "sys_uart_config.h"
#include "sys_uart_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UART ports as providers (SYS_DATA_PROVIDER.MD): the console's, always there, the board's link to another board
 * (CONFIG_SYS_UART_LINK), and the ports CREATE makes at run time, each under the id it is given. A port is a provider with one user: a session
 * (framed), a stream's binding or the command binding (raw). A framed port is wired and carries a whole frame on a
 * line COBS-encoded with its CRC16, between 0x00 bytes:
 *
 *   00 cobs([frame][crc16 LE]) 00
 *
 * RX: the bytes up to each 0x00 are one candidate: decoded, its CRC checked, delivered whole to the provider's user;
 * anything else (the ROM's text, a panic) is dropped. TX: the provider's send queues the frame, the port's TX task
 * writes it. A raw port carries the bytes as they are. A DIRECT port has no tasks and no hook: a device driver writes and
 * reads it on its own task (sys_uart_write, sys_uart_read).
 */

/** @brief What CREATE takes (sys_uart_router.h); a UART port's rate, format and pins. */
typedef struct sys_uart_cfg sys_uart_cfg_t;

/**
 * @brief Installs the console drivers (stdout moves to the UART driver), adds the console's provider and starts the
 * RX and TX tasks; with CONFIG_SYS_UART_LINK the same for the link's UART. Call once, before the session attaches
 * them.
 * @param console_provider  The console's provider id.
 * @param link_provider     The link's provider id, without CONFIG_SYS_UART_LINK unused.
 */
MUST_USE err_h sys_uart_init(uint8_t console_provider, uint8_t link_provider);

/**
 * @brief Makes a UART port as the provider cfg->provider: its driver, rings and tasks. It is
 * removed with sys_data_provider_remove() once no one has it open.
 * @param cfg  The port, its rate, mode, format and pins (the pins of the ESP32-S3's GPIO device, which must exist).
 * @return NULL; ERR_UART_BAD_ARG, _BAD_PORT, _PORT_EXISTS, _BAD_BAUD, _BAD_FORMAT, _BAD_PIN, _INIT, _NO_MEM, or the
 * provider registry's (ERR_PROV_EXISTS, ERR_PROV_FULL).
 */
MUST_USE err_h sys_uart_create(const sys_uart_cfg_t* cfg);

/**
 * @brief What a port made by sys_uart_create() was made with.
 * @param port  The UART.
 * @param cfg   The answer.
 * @return NULL; ERR_UART_BAD_ARG, ERR_UART_BAD_PORT (none made so; the console's and the link's are the board's own).
 */
MUST_USE err_h sys_uart_get(uint8_t port, sys_uart_cfg_t* cfg);

/**
 * @brief One command and its answer on a raw or DIRECT provider, as I2C's register read is: what goes out, then the first message
 * that comes back (RAW: up to a pause in the line; RAW_LINE: a line). It holds the provider's rx hook for that time, so
 * it refuses a provider someone reads from, and what came before it is not seen. Waits on the caller's task.
 * @param provider  The raw provider.
 * @param tx        What goes out; NULL with tx_len 0: only waits for what comes.
 * @param tx_len    Bytes in `tx`.
 * @param rx        Where the answer goes.
 * @param rx_cap    Bytes `rx` holds; a longer answer is cut.
 * @param rx_len    The answer's bytes; 0: nothing came in `wait_ms`, which is no error.
 * @param wait_ms   How long to wait for it, at most SYS_UART_RAW_WAIT_MAX_MS; 0: only sends.
 * @return NULL; ERR_UART_BAD_ARG, ERR_UART_NOT_RAW, the provider's ERR_PROV_NOT_FOUND, ERR_PROV_IN_USE, ERR_PROV_BUSY.
 */
MUST_USE err_h sys_uart_transact(uint8_t provider, const uint8_t* tx, uint32_t tx_len, uint8_t* rx, uint32_t rx_cap,
                                 uint32_t* rx_len, uint32_t wait_ms);

/**
 * @brief Writes to a DIRECT port on the caller's task and returns when the last byte has left the line (the port's own
 * tasks, it has none, add no delay). Hold the provider first: sys_data_provider_open(provider, NULL, NULL).
 * @param provider  The DIRECT port's provider.
 * @param data      What goes out.
 * @param len       Bytes in `data`.
 * @param wait_ms   How long to wait for the line.
 * @return NULL; ERR_UART_BAD_ARG, ERR_UART_NOT_DIRECT, ERR_UART_TIMEOUT.
 */
MUST_USE err_h sys_uart_write(uint8_t provider, const uint8_t* data, uint32_t len, uint32_t wait_ms);

/**
 * @brief Reads from a DIRECT port on the caller's task. It returns as soon as `cap` bytes came, so a reply of a known
 * length has no delay of its own; else at a pause of `idle_ms` after the first byte (between one and two of it: the
 * resolution of the wait), or after `wait_ms` with nothing. Never call it under a lock others wait for: it can wait
 * `wait_ms`, and a device driver's op runs under the device registry's (SYS_DEVICE.MD "Concurrency").
 * @param provider  The DIRECT port's provider.
 * @param data      Where it goes.
 * @param cap       Bytes `data` holds.
 * @param len       The bytes read; 0: nothing came, which is no error.
 * @param wait_ms   How long to wait for the first byte.
 * @param idle_ms   The pause that ends it; 0: only `cap` or `wait_ms` end it.
 * @return NULL; ERR_UART_BAD_ARG, ERR_UART_NOT_DIRECT.
 */
MUST_USE err_h sys_uart_read(uint8_t provider, uint8_t* data, uint32_t cap, uint32_t* len, uint32_t wait_ms,
                             uint32_t idle_ms);

/** @brief Drops what came in and was not read: before a command, so its answer is the first thing read. */
MUST_USE err_h sys_uart_flush_input(uint8_t provider);

/**
 * @brief Sets a DIRECT port's rate, once what it wrote has left the line.
 * @param provider  The DIRECT port's provider.
 * @param baud      SYS_UART_BAUD_MIN..SYS_UART_BAUD_MAX.
 * @return NULL; ERR_UART_BAD_BAUD, ERR_UART_NOT_DIRECT, the driver's error.
 */
MUST_USE err_h sys_uart_set_baud(uint8_t provider, uint32_t baud);

/**
 * @brief Lends the link (CONFIG_SYS_UART_LINK) as a DIRECT port, or takes it back (SYS_UART.MD "The link lent"). Lent:
 * the provider goes down (its session ends), its tasks stop, what was queued is dropped, and its owner writes and reads
 * on its own task (sys_uart_write, sys_uart_read, sys_uart_set_baud under the link's provider id). Taken back: the
 * link's rate, its tasks, the provider up (its session says hello). For sys_flasher, on one task.
 * @param direct  true: lend it; false: take it back.
 * @return NULL, also when it already is so; ERR_UART_BAD_PORT (no link), ERR_UART_NO_MEM, the driver's error.
 */
MUST_USE err_h sys_uart_link_direct(bool direct);

#ifdef __cplusplus
}
#endif
