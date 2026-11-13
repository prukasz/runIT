#pragma once

/**
 * @file sys_uart_config.h
 * @brief What a UART port is made with: its mode, parity and the rates it takes (wire format: never renumber).
 * Included by sys_uart.h; published to the app by templates/generate_enums.py.
 */

/** @brief What the port carries. Set at CREATE. */
//#ref-enum @alias UART mode
typedef enum {
  SYS_UART_MODE_FRAMED = 0,    //@alias Framed @description COBS and CRC16 frames: a session can attach it (the link to another board)
  SYS_UART_MODE_RAW = 1,       //@alias Raw @description The bytes as they are; a message is what arrives before a pause in the line
  SYS_UART_MODE_RAW_LINE = 2,  //@alias Raw lines @description The bytes as they are; a message ends at a line feed (a GPS's sentences)
  SYS_UART_MODE_DIRECT = 3,    //@alias Direct @description No tasks of its own: a device driver writes and reads on its own task, for protocols with exact timing
} sys_uart_mode_e;

/** @brief The parity bit. */
//#ref-enum @alias UART parity
typedef enum {
  SYS_UART_PARITY_NONE = 0,  //@alias None @description No parity bit
  SYS_UART_PARITY_EVEN = 1,  //@alias Even @description Even parity
  SYS_UART_PARITY_ODD = 2,   //@alias Odd @description Odd parity
} sys_uart_parity_e;

/* How long RAW and RECEIVE wait for an answer: the router's work task waits with them. */
//#ref-defines uart_raw @alias UART raw command @prefix SYS_UART_RAW_
#define SYS_UART_RAW_WAIT_MAX_MS 1000u  //@alias Longest wait @description The longest a raw command waits for an answer (ms)

/* The rates CREATE takes: any rate between, not a list (DMX is 250000, MIDI 31250). */
//#ref-defines uart_baud @alias UART baud rate @prefix SYS_UART_BAUD_
#define SYS_UART_BAUD_MIN 1200u     //@alias Lowest @description The lowest baud rate
#define SYS_UART_BAUD_MAX 5000000u  //@alias Highest @description The highest baud rate (a short cable)
