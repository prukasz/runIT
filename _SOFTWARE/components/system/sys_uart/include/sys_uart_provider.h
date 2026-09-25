#pragma once
#include <stdint.h>
#include <sdkconfig.h>
#include "sys_error.h"

/**
 * @file sys_uart_provider.h
 * @brief The console UART as a sys_data_connector transport provider.
 *
 * The console UART also carries the text log, so frames travel as text lines
 * that a reader can pick out of the log:
 *
 *     #R:<hex bytes>\n        (both directions, uppercase on TX, either case on RX)
 *
 * - **Endpoint:** only 0 (the console UART).
 * - **Framing:** one line is one frame. TX lines are written through stdout
 *   under its lock, so they never interleave with a log line. RX lines that
 *   don't start with the prefix are ignored; a bad or too-long hex line is
 *   dropped and reported (`ERR_UART_LINE_BAD`).
 * - **Frame limit:** CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX (no `max_frame` op).
 * - **Peer:** always 0.
 * - **RX binding:** one connector at a time.
 *
 * @code
 * SE_TRY(sys_uart_provider_register(RUNIT_DATA_PROVIDER_UART));
 * SE_TRY(sys_data_connector_bind_rx(SYS_DATA_CONNECTOR_INTERFACE, RUNIT_DATA_PROVIDER_UART, SYS_UART_ENDPOINT_CONSOLE));
 * @endcode
 */

/** @brief Also read frames from the native USB (USB-Serial-JTAG) when it is a
 *  console output: it gets the same stdout lines, so it becomes a full link. */
#define SYS_UART_USB_JTAG (CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)

/** @brief The only endpoint: the console UART. */
#define SYS_UART_ENDPOINT_CONSOLE 0u

/**
 * @brief Install the UART driver on the console UART (stdout moves to the
 * driver), start the RX task and register the provider under @p provider_id.
 *
 * @return err_h NULL on success; ERR_ESP_ERR from the driver, or
 * sys_data_connector_register_provider()'s error.
 */
SE_MUST_USE err_h sys_uart_provider_register(uint8_t provider_id);
