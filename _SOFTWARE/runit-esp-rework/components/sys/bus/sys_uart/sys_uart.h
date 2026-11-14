#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"
#include "sdkconfig.h"
#include "sys_data_provider.h"
#include "sys_uart_config.h"
#include "sys_uart_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The board's UART ports as providers (SYS_UART.MD): the console's, always there, and the link to another
 * board (CONFIG_SYS_UART_LINK). Both are framed and wired: a whole frame on a line COBS-encoded with its CRC16,
 * between 0x00 bytes:
 *
 *   00 cobs([frame][crc16 LE]) 00
 *
 * RX: the bytes up to each 0x00 are one candidate: decoded, its CRC checked, delivered whole to the provider's user;
 * anything else (the ROM's text, a panic) is dropped. TX: the provider's send queues the frame (its two parts and
 * their CRC as one item), the port's TX task writes it. The link can be lent as a DIRECT port: its owner writes and
 * reads on its own task (sys_uart_write, sys_uart_read).
 */

/**
 * @brief Installs the console drivers (stdout moves to the UART driver), adds the console's provider and starts its
 * RX and TX tasks; with CONFIG_SYS_UART_LINK the same for the link's UART, whose pins it reserves in ESP-IDF. Once, at
 * boot, before a user opens them and before the GPIO device is made (it then refuses the link's pins).
 * @param[in] console_provider The console's provider id.
 * @param[in] link_provider    The link's provider id; unused without CONFIG_SYS_UART_LINK.
 * @return The error of the drivers (ERR_UART_INIT, ERR_ESP_ERR) or of the provider registry
 */
MUST_USE err_h sys_uart_init(uint8_t console_provider, uint8_t link_provider);

/**
 * @brief Writes to a DIRECT port on the caller's task and returns when the last byte has left the line (the port has
 * no tasks to add a delay). Hold the provider first: sys_data_provider_open(provider, NULL, NULL).
 * @param[in] provider The DIRECT port's provider.
 * @param[in] data     What goes out.
 * @param[in] len      Bytes in `data`.
 * @param[in] wait_ms  How long to wait for the line.
 * @return NULL; ERR_UART_BAD_ARG, ERR_UART_NOT_DIRECT, ERR_UART_TIMEOUT.
 */
MUST_USE err_h sys_uart_write(uint8_t provider, const uint8_t* data, uint32_t len, uint32_t wait_ms);

/**
 * @brief Reads from a DIRECT port on the caller's task. It returns as soon as `cap` bytes came, so a reply of a known
 * length has no delay of its own; else at a pause of `idle_ms` after the first byte (between one and two of it: the
 * resolution of the wait), or after `wait_ms` with nothing. Never call it under a lock others wait for: it can wait
 * `wait_ms`.
 * @param[in]  provider The DIRECT port's provider.
 * @param[out] data     Where it goes.
 * @param[in]  cap      Bytes `data` holds.
 * @param[out] len      The bytes read; 0: nothing came, which is no error.
 * @param[in]  wait_ms  How long to wait for the first byte.
 * @param[in]  idle_ms  The pause that ends it; 0: only `cap` or `wait_ms` end it.
 * @return NULL; ERR_UART_BAD_ARG, ERR_UART_NOT_DIRECT.
 */
MUST_USE err_h sys_uart_read(uint8_t provider, uint8_t* data, uint32_t cap, uint32_t* len, uint32_t wait_ms,
                             uint32_t idle_ms);

/**
 * @brief Drops what came in on a DIRECT port and was not read: before a command, so its answer is the first thing read.
 * @param[in] provider The DIRECT port's provider
 * @return NULL; ERR_UART_NOT_DIRECT, the driver's error
 */
MUST_USE err_h sys_uart_flush_input(uint8_t provider);

/**
 * @brief Sets a DIRECT port's rate, once what it wrote has left the line.
 * @param[in] provider The DIRECT port's provider.
 * @param[in] baud     SYS_UART_BAUD_MIN..SYS_UART_BAUD_MAX.
 * @return NULL; ERR_UART_BAD_BAUD, ERR_UART_NOT_DIRECT, the driver's error.
 */
MUST_USE err_h sys_uart_set_baud(uint8_t provider, uint32_t baud);

/**
 * @brief Lends the link (CONFIG_SYS_UART_LINK) as a DIRECT port, or takes it back (SYS_UART.MD "The link lent"). Lent:
 * the provider goes down (its session ends), its tasks park, what was queued is dropped, and its owner writes and reads
 * on its own task (sys_uart_write, sys_uart_read, sys_uart_flush_input, sys_uart_set_baud under the link's provider
 * id). Taken back: the console's rate again, the tasks woken, the provider up (its session says hello). For
 * sys_flasher, on one task.
 * @param[in] direct true: lend it; false: take it back.
 * @return NULL, also when it already is so; ERR_UART_BAD_PORT (no link), ERR_UART_TIMEOUT (a task did not park in
 *         PARK_WAIT_MS: the link is not lent and stays up), the driver's error.
 */
MUST_USE err_h sys_uart_link_direct(bool direct);

#ifdef __cplusplus
}
#endif
