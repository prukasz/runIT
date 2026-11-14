#pragma once

/**
 * @file sys_uart_config.h
 * @brief What a UART port is: its mode and the rates it takes (wire format: never renumber). Included by sys_uart.h;
 * published to the app by templates/generate_enums.py.
 */

/** @brief What the port carries. The board's ports are FRAMED; a port lent (the contract's LEND) is DIRECT. */
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

/* How long the contract's SEND and RECEIVE wait: the caller's task (a router task for a command) waits with them. */
//#ref-defines uart_wait @alias UART wait @prefix SYS_UART_WAIT_
#define SYS_UART_WAIT_MAX_MS 1000u  //@alias Longest wait @description The longest SEND waits for room and RECEIVE for a message (ms)

/* The rates a DIRECT port is set to (sys_uart_set_baud): any rate between, not a list. */
//#ref-defines uart_baud @alias UART baud rate @prefix SYS_UART_BAUD_
#define SYS_UART_BAUD_MIN 1200u     //@alias Lowest @description The lowest baud rate
#define SYS_UART_BAUD_MAX 5000000u  //@alias Highest @description The highest baud rate (a short cable)
