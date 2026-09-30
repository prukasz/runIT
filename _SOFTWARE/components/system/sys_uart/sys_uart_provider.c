#include "sys_uart_provider.h"

#include <stdio.h>
#include <string.h>
#include <driver/uart.h>
#include <driver/uart_vfs.h>
#if SYS_UART_USB_JTAG
#include <driver/usb_serial_jtag.h>
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <sdkconfig.h>
#include "sys_data_connector.h"
#include "utils.h"

#define UART_PORT CONFIG_ESP_CONSOLE_UART_NUM
#define USB_JTAG_TX_BUF 64u /* the driver needs one; TX goes through stdout */
#define LINE_PREFIX "#R:"
#define LINE_PREFIX_LEN (sizeof(LINE_PREFIX) - 1u)
#define FRAME_LINE_MAX (LINE_PREFIX_LEN + 2u * CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX)
#define READ_CHUNK 64u
#define READ_WAIT_MS 10u
#define NO_CONNECTOR UINT8_MAX

_Static_assert(CONFIG_SYS_UART_RX_QUEUE_SIZE % 4 == 0, "SYS_UART_RX_QUEUE_SIZE must be a multiple of 4");

R_RINGBUFFER_DEFINE(sys_uart_rx_rb, CONFIG_SYS_UART_RX_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
R_TASK_DEFINE(sys_uart_rx_task_h, CONFIG_SYS_UART_TASK_STACK_SIZE);

/* Connector woken when a frame is queued; written by bind/unbind, read by the RX task. */
static uint8_t s_rx_conn = NO_CONNECTOR;

// -----------------------------------------------------------------------------
// Provider operations
// -----------------------------------------------------------------------------

#define OWNER OWNER_SYS_UART_PROVIDER

static SE_MUST_USE err_h check_endpoint(uint32_t endpoint) {
  if (endpoint != SYS_UART_ENDPOINT_CONSOLE) {
    SE_FAIL(ERR_INVALID_VAL_UI32, .val = endpoint, .min = SYS_UART_ENDPOINT_CONSOLE, .max = SYS_UART_ENDPOINT_CONSOLE);
  }
  return NULL;
}

/* One frame = one "#R:<hex>\n" line, written under the stdout lock so a log
   line from another task can't land inside it. */
static SE_MUST_USE err_h uart_provider_send(uint32_t endpoint, uint32_t peer, const uint8_t* frame, size_t len) {
  (void)peer;
  SE_TRY(check_endpoint(endpoint));
  static const char hex[] = "0123456789ABCDEF";
  char chunk[READ_CHUNK];
  size_t n = 0;
  flockfile(stdout);
  fputs(LINE_PREFIX, stdout);
  for (size_t i = 0; i < len; i++) {
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
  return NULL;
}

static SE_MUST_USE err_h uart_provider_dequeue(uint32_t endpoint, uint8_t* buf, size_t max_len, size_t* out_len, uint32_t* out_peer) {
  *out_peer = 0;
  *out_len = 0;
  SE_TRY(check_endpoint(endpoint));
  size_t size = 0;
  uint8_t* item = xRingbufferReceive(sys_uart_rx_rb, &size, 0);
  if (item == NULL) return NULL;
  if (size > max_len) {
    vRingbufferReturnItem(sys_uart_rx_rb, item);
    SE_FAIL(ERR_UART_FRAME_TOO_LONG, .len = (uint16_t)size, .max = (uint16_t)max_len);
  }
  memcpy(buf, item, size);
  vRingbufferReturnItem(sys_uart_rx_rb, item);
  *out_len = size;
  return NULL;
}

static SE_MUST_USE err_h uart_provider_bind_rx(uint32_t endpoint, uint8_t conn_id) {
  SE_TRY(check_endpoint(endpoint));
  uint8_t expected = NO_CONNECTOR;
  if (!__atomic_compare_exchange_n(&s_rx_conn, &expected, conn_id, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) && expected != conn_id) {
    SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  }
  if (xRingbufferGetCurFreeSize(sys_uart_rx_rb) < CONFIG_SYS_UART_RX_QUEUE_SIZE) sys_data_connector_notify_rx(conn_id);
  return NULL;
}

static SE_MUST_USE err_h uart_provider_unbind_rx(uint32_t endpoint, uint8_t conn_id) {
  SE_TRY(check_endpoint(endpoint));
  uint8_t expected = conn_id;
  __atomic_compare_exchange_n(&s_rx_conn, &expected, NO_CONNECTOR, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
  return NULL;
}

// -----------------------------------------------------------------------------
// RX task: console bytes -> "#R:" lines -> frames
// -----------------------------------------------------------------------------

#undef OWNER
#define OWNER OWNER_SYS_UART_RX

/* One line assembler per input (UART0, USB-Serial-JTAG). */
typedef struct {
  char line[FRAME_LINE_MAX];
  size_t len;
  bool overflow;
} line_rx_t;

static uint8_t s_frame[CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX];

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

/* Decode one complete line; lines without the prefix are not frames. */
static void rx_line(const char* line, size_t len, bool overflow) {
  if (len < LINE_PREFIX_LEN || memcmp(line, LINE_PREFIX, LINE_PREFIX_LEN) != 0) return;
  const char* hex = line + LINE_PREFIX_LEN;
  size_t digits = len - LINE_PREFIX_LEN;
  if (overflow || digits == 0 || (digits & 1u)) {
    SE_RAISE(ERR_UART_LINE_BAD, .len = (uint16_t)len);
    return;
  }
  size_t size = digits / 2u;
  for (size_t i = 0; i < size; i++) {
    int hi = hex_nibble(hex[2 * i]);
    int lo = hex_nibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) {
      SE_RAISE(ERR_UART_LINE_BAD, .len = (uint16_t)len);
      return;
    }
    s_frame[i] = (uint8_t)((hi << 4) | lo);
  }
  if (xRingbufferSend(sys_uart_rx_rb, s_frame, size, 0) != pdTRUE) {
    SE_RAISE(ERR_UART_RX_FULL, .len = (uint16_t)size);
    return;
  }
  uint8_t conn = __atomic_load_n(&s_rx_conn, __ATOMIC_ACQUIRE);
  if (conn != NO_CONNECTOR) sys_data_connector_notify_rx(conn);
}

static void rx_bytes(line_rx_t* rx, const uint8_t* data, int count) {
  for (int i = 0; i < count; i++) {
    char c = (char)data[i];
    if (c == '\n' || c == '\r') {
      if (rx->len > 0) rx_line(rx->line, rx->len, rx->overflow);
      rx->len = 0;
      rx->overflow = false;
    } else if (rx->len < sizeof(rx->line)) {
      rx->line[rx->len++] = c;
    } else {
      rx->overflow = true;
    }
  }
}

static line_rx_t s_uart_rx;
#if SYS_UART_USB_JTAG
static line_rx_t s_usb_rx;
#endif

static void sys_uart_rx_task(void* arg) {
  (void)arg;
  uint8_t chunk[READ_CHUNK];
  while (1) {
    rx_bytes(&s_uart_rx, chunk, uart_read_bytes(UART_PORT, chunk, sizeof(chunk), pdMS_TO_TICKS(READ_WAIT_MS)));
#if SYS_UART_USB_JTAG
    rx_bytes(&s_usb_rx, chunk, usb_serial_jtag_read_bytes(chunk, sizeof(chunk), pdMS_TO_TICKS(READ_WAIT_MS)));
#endif
  }
}

// -----------------------------------------------------------------------------
// Registration
// -----------------------------------------------------------------------------

#undef OWNER
#define OWNER OWNER_SYS_UART_INIT

err_h sys_uart_provider_register(uint8_t provider_id) {
  static const sys_data_connector_provider_t s_uart_provider = {
      .name = "UART",
      .send = uart_provider_send,
      .dequeue = uart_provider_dequeue,
      .bind_rx = uart_provider_bind_rx,
      .unbind_rx = uart_provider_unbind_rx,
  };
  if (!uart_is_driver_installed(UART_PORT)) {
    SE_TRY_ESP(uart_driver_install(UART_PORT, CONFIG_SYS_UART_DRIVER_RX_BUF, 0, 0, NULL, 0));
    fflush(stdout);
    uart_vfs_dev_use_driver(UART_PORT);
  }
#if SYS_UART_USB_JTAG
  if (!usb_serial_jtag_is_driver_installed()) {
    usb_serial_jtag_driver_config_t usb_cfg = {.tx_buffer_size = USB_JTAG_TX_BUF, .rx_buffer_size = CONFIG_SYS_UART_DRIVER_RX_BUF};
    SE_TRY_ESP(usb_serial_jtag_driver_install(&usb_cfg));
  }
#endif
  if (sys_uart_rx_task_h == NULL) {
    R_TASK_START(sys_uart_rx_task_h, sys_uart_rx_task, NULL, CONFIG_SYS_UART_TASK_PRIO);
  }
  SE_TRY(sys_data_connector_register_provider(provider_id, &s_uart_provider));
  return NULL;
}
