#pragma once

/**
 * @file sys_uart_config.h
 * @brief What a UART port is: its mode and the rates it takes (wire format: never renumber). Included by sys_uart.h;
 * published to the app by templates/generate_enums.py.
 */

/** @brief What the port carries. The board's ports are FRAMED; the link lent is DIRECT. */
//#ref-enum @alias UART mode
typedef enum {
  SYS_UART_MODE_FRAMED = 0,  //@alias Framed @description COBS and CRC16 frames: a session can attach it (the link to another board)
  SYS_UART_MODE_DIRECT = 3,  //@alias Direct @description No tasks of its own: its owner writes and reads on its own task, for protocols with exact timing (the flasher)
} sys_uart_mode_e;

/* The rates a DIRECT port is set to (sys_uart_set_baud): any rate between, not a list. */
//#ref-defines uart_baud @alias UART baud rate @prefix SYS_UART_BAUD_
#define SYS_UART_BAUD_MIN 1200u     //@alias Lowest @description The lowest baud rate
#define SYS_UART_BAUD_MAX 5000000u  //@alias Highest @description The highest baud rate (a short cable)
