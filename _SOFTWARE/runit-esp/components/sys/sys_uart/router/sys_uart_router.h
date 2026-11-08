#pragma once

#include "router.h"
#include "sys_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_UART (SYS_UART.MD "Remote"), each above the frame it uses (packed, little-endian;
 * contract_uart.json names them), with its sides. Never renumber. A port made here is removed with the provider's
 * REMOVE (SYS_DATA_PROVIDER.MD), by the id it was given. */

#define SYS_UART_OP_CREATE 0x01u  // R: sys_uart_port_args_t -> sys_uart_cfg_t; W: sys_uart_cfg_t
typedef struct __attribute__((packed)) {
  uint8_t port;  /**< The UART of the chip */
} sys_uart_port_args_t;

typedef struct __attribute__((packed)) sys_uart_cfg {
  uint8_t provider;   /**< Its id, chosen here: any the board has not given */
  uint8_t port;       /**< The UART of the chip: not the console's, not the link's */
  uint32_t baud;      /**< SYS_UART_BAUD_MIN..SYS_UART_BAUD_MAX */
  uint8_t mode;       /**< sys_uart_mode_e */
  uint8_t parity;     /**< sys_uart_parity_e */
  uint8_t stop_bits;  /**< 1 or 2; 8 data bits */
  sys_io_pin_t tx;    /**< A pin of the ESP32-S3's GPIO device */
  sys_io_pin_t rx;    /**< A pin of the ESP32-S3's GPIO device */
  sys_io_pin_t rts;   /**< With CTS: hardware flow control; SYS_IO_PIN_NONE: none */
  sys_io_pin_t cts;   /**< With RTS; SYS_IO_PIN_NONE: none */
} sys_uart_cfg_t;

/* A command to what hangs on a raw port and its answer, as I2C's RAW. The answer is what came back, empty when nothing did. */
#define SYS_UART_OP_RAW     0x02u  // W: sys_uart_raw_args_t + data -> the bytes that came back
#define SYS_UART_OP_RECEIVE 0x03u  // R: sys_uart_raw_args_t -> the bytes that came
typedef struct __attribute__((packed)) {
  uint8_t provider;  /**< A raw provider */
  uint16_t wait_ms;  /**< How long to wait for the answer, at most SYS_UART_RAW_WAIT_MAX_MS */
  uint16_t rx_max;   /**< The answer's bytes at most; a longer one is cut */
} sys_uart_raw_args_t;

/**
 * @brief The UART ports made at run time, under ROUTER_KEY_UART.
 * Ops and layouts: SYS_UART.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_UART, sys_uart_router));
 */
ROUTER_MODULE_DECLARE(sys_uart_router);

#ifdef __cplusplus
}
#endif
