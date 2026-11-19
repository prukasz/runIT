#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "cmd_table.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "errors.h"
#include "sdkconfig.h"
#include "sys_data_provider_driver.h"
#include "sys_uart_codec.h"
#include "utils.h"

#define USB_JTAG (CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
#if USB_JTAG
#include "driver/usb_serial_jtag.h"
#endif

#define CONSOLE_PORT    CONFIG_ESP_CONSOLE_UART_NUM
#define USB_JTAG_TX_BUF 512u                        // the driver's TX buffer: a frame in a few writes
#define USB_JTAG_PIECE  (USB_JTAG_TX_BUF / 2u)      // the longest write the driver takes at once
#define UART_TX_BUF     4096u                       // the console's UART TX buffer: a burst waits there, not here
#define READ_CHUNK      64u
#define READ_WAIT_MS    10u

U_TASK_DEFINE(console_rx_task_h, CONFIG_PROVIDER_CONSOLE_RX_TASK_STACK);
U_TASK_DEFINE(console_tx_task_h, CONFIG_PROVIDER_CONSOLE_TX_TASK_STACK);

typedef struct {
  uint8_t buf[SYS_UART_WIRE_MAX];
  uint32_t len;
  bool overflow;  // longer than any frame: dropped at its 0x00
} line_rx_t;

typedef struct {
  sys_data_provider_t* provider;  // slot-owned handle; set by console_create()
  uart_port_t uart;
  sys_uart_txq_t txq;  // made once: the console is never removed for good
  line_rx_t uart_rx;
#if USB_JTAG
  line_rx_t usb_rx;
#endif
} console_port_t;

static console_port_t s_console;

/* ----------------------------------------------------------------------------
 * TX: the provider's send only queues the frame; tx_task writes it as the line takes it, the urgent ones first
 * ------------------------------------------------------------------------- */

#if USB_JTAG
// A frame to the USB-Serial-JTAG in pieces the driver takes, until the host stops reading.
static void write_usb(const uint8_t* data, uint32_t len) {
  uint32_t done = 0;
  while (done < len && usb_serial_jtag_is_connected()) {
    const uint32_t piece = len - done < USB_JTAG_PIECE ? len - done : USB_JTAG_PIECE;
    const int n = usb_serial_jtag_write_bytes(data + done, piece, MSEC(CONFIG_PROVIDER_CONSOLE_SEND_WAIT_MS));
    if (n <= 0) {
      return;
    }
    done += (uint32_t)n;
  }
}
#endif

/* The console's UART at its baud rate is far slower than USB: a frame that does not fit its TX buffer now is not
 * written there (its host misses it, as a dropped frame), so a USB host is never held to the UART's pace. */
static void write_out(const console_port_t* p, const uint8_t* data, uint32_t len) {
  size_t room = 0;
  if (uart_get_tx_buffer_free_size(p->uart, &room) == ESP_OK && room >= len) {
    uart_write_bytes(p->uart, data, len);
  }
#if USB_JTAG
  write_usb(data, len);
#endif
}

static void tx_task(void* arg) {
  console_port_t* p = arg;
  while (true) {
    write_out(p, p->txq.line, sys_uart_txq_pop(&p->txq, portMAX_DELAY));
  }
}

// The port's input: the packet into the TX queue with its flags (NOWAIT: no room, dropped at once; else
// CONFIG_PROVIDER_CONSOLE_SEND_WAIT_MS on the sender's task). What the queue does not take is lost.
static void provider_pass(data_stream_port_t* self, data_stream_port_id_t from, uint8_t flags, const uint8_t* data,
                          uint32_t len) {
  (void)from;
  console_port_t* p = SYS_DATA_PROVIDER_OF(self)->driver;
  (void)sys_uart_txq_push(&p->txq, (cmd_in_t){.data = data, .len = len}, flags,
                          MSEC(CONFIG_PROVIDER_CONSOLE_SEND_WAIT_MS));
}

/* ----------------------------------------------------------------------------
 * RX: bytes -> the bytes up to each 0x00 -> COBS, CRC16 -> one frame to the provider's user
 * ------------------------------------------------------------------------- */

/* One candidate: decoded, its CRC checked, delivered whole. Anything else (the ROM's text, a panic, typing, a line
 * hit by noise) fails and is dropped without a word: the host's tool shows those bytes as text. */
static void rx_candidate(console_port_t* p, uint8_t* buf, uint32_t len) {
  sys_data_provider_t* prov = p->provider;
  if (prov == NULL) {
    return;
  }
  const uint32_t n = sys_uart_unframe(buf, len);
  if (n > 0) {
    sys_data_provider_deliver(prov, buf, n);
  }
}

static void rx_bytes(console_port_t* p, line_rx_t* rx, const uint8_t* data, int count) {
  for (int i = 0; i < count; i++) {
    if (data[i] == 0) {
      if (rx->len > 0 && !rx->overflow) {
        rx_candidate(p, rx->buf, rx->len);
      }
      rx->len = 0;
      rx->overflow = false;
    } else if (rx->len < sizeof(rx->buf)) {
      rx->buf[rx->len++] = data[i];
    } else {
      rx->overflow = true;
    }
  }
}

static void rx_task(void* arg) {
  console_port_t* p = arg;
  uint8_t chunk[READ_CHUNK];
  while (true) {
    rx_bytes(p, &p->uart_rx, chunk, uart_read_bytes(p->uart, chunk, sizeof(chunk), MSEC(READ_WAIT_MS)));
#if USB_JTAG
    rx_bytes(p, &p->usb_rx, chunk, usb_serial_jtag_read_bytes(chunk, sizeof(chunk), MSEC(READ_WAIT_MS)));
#endif
  }
}

/* ----------------------------------------------------------------------------
 * Provider driver registration (SYS_DATA_PROVIDER_TYPE_CONSOLE)
 * ------------------------------------------------------------------------- */

static err_h console_create(sys_data_provider_t* p, void* ctx, const void* cfg) {
  (void)ctx;
  (void)cfg;
  ERR_CHECK(s_console.provider == NULL, ERR_PROV_IN_USE, .provider = p->id);
  if (!uart_is_driver_installed(CONSOLE_PORT)) {
    ERR_TRY_ESP(uart_driver_install(CONSOLE_PORT, CONFIG_PROVIDER_CONSOLE_DRIVER_RX_BUF, UART_TX_BUF, 0, NULL, 0));
    fflush(stdout);
    uart_vfs_dev_use_driver(CONSOLE_PORT);
  }
#if USB_JTAG
  if (!usb_serial_jtag_is_driver_installed()) {
    usb_serial_jtag_driver_config_t usb_cfg = {.tx_buffer_size = USB_JTAG_TX_BUF,
                                               .rx_buffer_size = CONFIG_PROVIDER_CONSOLE_DRIVER_RX_BUF};
    ERR_TRY_ESP(usb_serial_jtag_driver_install(&usb_cfg));
  }
#endif
  s_console.uart = CONSOLE_PORT;
  if (console_tx_task_h == NULL) {
    ERR_CHECK(sys_uart_txq_init(&s_console.txq, CONFIG_PROVIDER_CONSOLE_TX_QUEUE, true), ERR_PROV_NO_MEM,
              .provider = p->id, .size = CONFIG_PROVIDER_CONSOLE_TX_QUEUE);
    U_TASK_START(console_tx_task_h, tx_task, &s_console, CONFIG_PROVIDER_CONSOLE_TX_TASK_PRIO);
    s_console.txq.task = console_tx_task_h;
  }
  s_console.provider = p;
  p->driver = &s_console;
  p->port.pass = provider_pass;
  p->state |= SYS_DATA_PROVIDER_STATE_WIRED | SYS_DATA_PROVIDER_STATE_FRAMED | SYS_DATA_PROVIDER_STATE_UP;
  if (console_rx_task_h == NULL) {
    U_TASK_START(console_rx_task_h, rx_task, &s_console, CONFIG_PROVIDER_CONSOLE_RX_TASK_PRIO);
  }
  return NULL;
}

static err_h console_remove(sys_data_provider_t* p, void* ctx) {
  (void)ctx;
  if (s_console.provider == p) {
    s_console.provider = NULL;
  }
  return NULL;
}

SYS_DATA_PROVIDER_TYPE_DEFINE(SYS_DATA_PROVIDER_TYPE_CONSOLE, cmd_none_t, cmd_none_t, console_create, console_remove,
                              NULL, 0)
