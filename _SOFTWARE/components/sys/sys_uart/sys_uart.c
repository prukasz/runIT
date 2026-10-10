#include "sys_uart.h"

#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "router.h"
#include "sys_connector.h"
#include "sdkconfig.h"

#define USB_JTAG (CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
#if USB_JTAG
#include "driver/usb_serial_jtag.h"
#endif

ERR_MODULE_DEFINE(ERR_MOD_SYS_UART, "sys_uart", ERR_SYS_UART_MAP)

#define UART_PORT CONFIG_ESP_CONSOLE_UART_NUM
#define USB_JTAG_TX_BUF 64u  // the driver needs one; TX goes through stdout
#define LINE_PREFIX "#R:"
#define LINE_PREFIX_LEN (sizeof(LINE_PREFIX) - 1u)
#define LINE_LEN_MAX (LINE_PREFIX_LEN + 2u * CONFIG_ROUTER_FRAME_MAX)
#define READ_CHUNK 64u
#define READ_WAIT_MS 10u

U_TASK_DEFINE(sys_uart_rx_task_h, CONFIG_SYS_UART_TASK_STACK);

/* ----------------------------------------------------------------------------
 * TX: router link (answers) and connector provider (streams), same line format
 * ------------------------------------------------------------------------- */

static void write_line(const uint8_t* frame, uint32_t len) {
  static const char hex[] = "0123456789ABCDEF";
  char chunk[READ_CHUNK];
  size_t n = 0;
  flockfile(stdout);
  fputs(LINE_PREFIX, stdout);
  for (uint32_t i = 0; i < len; i++) {
    chunk[n++] = hex[frame[i] >> 4];
    chunk[n++] = hex[frame[i] & 0x0F];
    if (n == sizeof(chunk)) {
      fwrite(chunk, 1, n, stdout);
      n = 0;
    }
  }
  fwrite(chunk, 1, n, stdout);
  fputc('\n', stdout);
  fflush(stdout);
  funlockfile(stdout);
}

/* One send for both tables: the router link's `peer` and the connector provider's `endpoint` are both the one
 * address a UART has, SYS_UART_ENDPOINT. A connector bound elsewhere is refused instead of writing to the wrong place. */
static err_h uart_send(uint32_t address, const uint8_t* frame, uint32_t len) {
  ERR_CHECK(address == SYS_UART_ENDPOINT, ERR_UART_BAD_ENDPOINT, .endpoint = address);
  write_line(frame, len);
  return NULL;
}

static const router_link_t k_link = {
    .name = "uart",
    .send = uart_send,
    .max_frame = NULL,  // CONFIG_ROUTER_FRAME_MAX
};

static const sys_connector_provider_t k_provider = {
    .name = "uart",
    .send = uart_send,
    .max_frame = NULL,  // CONFIG_SYS_CONNECTOR_FRAME_MAX
};

/* ----------------------------------------------------------------------------
 * RX: console bytes -> "#R:" lines -> router frames
 * ------------------------------------------------------------------------- */

// One line assembler per input (UART, USB-Serial-JTAG).
typedef struct {
  char line[LINE_LEN_MAX];
  size_t len;
  bool overflow;
} line_rx_t;

static line_rx_t s_uart_rx;
#if USB_JTAG
static line_rx_t s_usb_rx;
#endif
static uint8_t s_frame[CONFIG_ROUTER_FRAME_MAX];  // RX task only

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

// One complete line. Lines without the prefix are log noise / typing: ignored.
static void rx_line(const char* line, size_t len, bool overflow) {
  if (len < LINE_PREFIX_LEN || memcmp(line, LINE_PREFIX, LINE_PREFIX_LEN) != 0) {
    return;
  }
  const char* hex = line + LINE_PREFIX_LEN;
  size_t digits = len - LINE_PREFIX_LEN;
  if (overflow || digits == 0 || (digits & 1u)) {
    err_report(ERR_NEW(ERR_UART_LINE_BAD, .len = (uint16_t)len));
    return;
  }
  size_t size = digits / 2u;
  for (size_t i = 0; i < size; i++) {
    int hi = hex_nibble(hex[2 * i]);
    int lo = hex_nibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) {
      err_report(ERR_NEW(ERR_UART_LINE_BAD, .len = (uint16_t)len));
      return;
    }
    s_frame[i] = (uint8_t)((hi << 4) | lo);
  }
  err_report(router_submit(TRANSPORT_UART, 0, s_frame, (uint32_t)size));
}

static void rx_bytes(line_rx_t* rx, const uint8_t* data, int count) {
  for (int i = 0; i < count; i++) {
    char c = (char)data[i];
    if (c == '\n' || c == '\r') {
      if (rx->len > 0) {
        rx_line(rx->line, rx->len, rx->overflow);
      }
      rx->len = 0;
      rx->overflow = false;
    } else if (rx->len < sizeof(rx->line)) {
      rx->line[rx->len++] = c;
    } else {
      rx->overflow = true;
    }
  }
}

static void rx_task(void* arg) {
  (void)arg;
  uint8_t chunk[READ_CHUNK];
  while (1) {
    rx_bytes(&s_uart_rx, chunk, uart_read_bytes(UART_PORT, chunk, sizeof(chunk), MSEC(READ_WAIT_MS)));
#if USB_JTAG
    rx_bytes(&s_usb_rx, chunk, usb_serial_jtag_read_bytes(chunk, sizeof(chunk), MSEC(READ_WAIT_MS)));
#endif
  }
}

/* ----------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------- */

err_h sys_uart_init(void) {
  if (!uart_is_driver_installed(UART_PORT)) {
    ERR_TRY_ESP(uart_driver_install(UART_PORT, CONFIG_SYS_UART_DRIVER_RX_BUF, 0, 0, NULL, 0));
    fflush(stdout);
    uart_vfs_dev_use_driver(UART_PORT);
  }
#if USB_JTAG
  if (!usb_serial_jtag_is_driver_installed()) {
    usb_serial_jtag_driver_config_t cfg = {.tx_buffer_size = USB_JTAG_TX_BUF,
                                           .rx_buffer_size = CONFIG_SYS_UART_DRIVER_RX_BUF};
    ERR_TRY_ESP(usb_serial_jtag_driver_install(&cfg));
  }
#endif
  ERR_TRY(router_link_register(TRANSPORT_UART, &k_link));
  ERR_TRY(sys_connector_register_provider(TRANSPORT_UART, &k_provider));
  if (sys_uart_rx_task_h == NULL) {
    U_TASK_START(sys_uart_rx_task_h, rx_task, NULL, CONFIG_SYS_UART_TASK_PRIO);
  }
  return NULL;
}
