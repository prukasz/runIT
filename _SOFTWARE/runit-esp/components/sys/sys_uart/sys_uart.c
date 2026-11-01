#include "sys_uart.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "sdkconfig.h"
#include "sys_data_provider_driver.h"
#include "utils.h"

#define USB_JTAG (CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
#if USB_JTAG
#include "driver/usb_serial_jtag.h"
#endif

#define CONSOLE_PORT    CONFIG_ESP_CONSOLE_UART_NUM
#define USB_JTAG_TX_BUF 512u                           // the driver's TX buffer: a frame in a few writes
#define UART_TX_BUF     4096u                          // the UART driver's TX buffer: a burst waits there, not here
#define CRC_BYTES       2u                             // CRC16-CCITT after the frame, little-endian
#define RAW_MAX         (CMD_FRAME_MAX + CRC_BYTES)    // [frame][crc16]
#define COBS_MAX        (RAW_MAX + RAW_MAX / 254u + 1u)  // COBS adds at most one byte in 254, plus its first
#define READ_CHUNK      64u
#define READ_WAIT_MS    10u
#define URGENT_QUEUE    (SYS_UART_ITEM_BYTES(CMD_FRAME_MAX) * 2u)  // two urgent frames waiting at most

#define NOTIFY_FRAME (1u << 0)  // TX task: a frame queued

// What a queued frame takes in a no-split ring buffer: its bytes rounded to 4, and the ring's 8-byte header.
#define SYS_UART_ITEM_BYTES(len) ((((len) + 3u) & ~3u) + 8u)

_Static_assert(CONFIG_SYS_UART_TX_QUEUE % 4 == 0, "SYS_UART_TX_QUEUE must be a multiple of 4");
_Static_assert(CONFIG_SYS_UART_TX_QUEUE >= 2u * SYS_UART_ITEM_BYTES(CMD_FRAME_MAX), "a whole frame fits the queue");
#if CONFIG_SYS_UART_LINK
_Static_assert(CONFIG_SYS_UART_LINK_PORT != CONSOLE_PORT, "the link needs a UART of its own");
#endif

U_TASK_DEFINE(sys_uart_rx_task_h, CONFIG_SYS_UART_TASK_STACK);
U_TASK_DEFINE(sys_uart_tx_task_h, CONFIG_SYS_UART_TX_TASK_STACK);
U_RINGBUFFER_DEFINE(sys_uart_tx_rb, CONFIG_SYS_UART_TX_QUEUE, RINGBUF_TYPE_NOSPLIT);
U_RINGBUFFER_DEFINE(sys_uart_urgent_rb, URGENT_QUEUE, RINGBUF_TYPE_NOSPLIT);
#if CONFIG_SYS_UART_LINK
U_TASK_DEFINE(sys_uart_link_rx_task_h, CONFIG_SYS_UART_TASK_STACK);
U_TASK_DEFINE(sys_uart_link_tx_task_h, CONFIG_SYS_UART_TX_TASK_STACK);
U_RINGBUFFER_DEFINE(sys_uart_link_tx_rb, CONFIG_SYS_UART_TX_QUEUE, RINGBUF_TYPE_NOSPLIT);
U_RINGBUFFER_DEFINE(sys_uart_link_urgent_rb, URGENT_QUEUE, RINGBUF_TYPE_NOSPLIT);
#endif

// One candidate per input (UART, USB-Serial-JTAG): the bytes since the last 0x00.
typedef struct {
  uint8_t buf[COBS_MAX];
  uint32_t len;
  bool overflow;  // longer than any frame: dropped at its 0x00
} line_rx_t;

/* A UART as a provider: the console's, or the link to another board. Its tasks, rings and buffers are its own; the
 * console's also writes to and reads from the USB-Serial-JTAG. */
typedef struct {
  sys_data_provider_t provider;  // first: the driver's send gets a pointer to it
  uart_port_t uart;
  bool console;  // the console: a frame that does not fit the UART's TX buffer is dropped there, never waited for
  RingbufHandle_t tx_rb;
  RingbufHandle_t urgent_rb;
  TaskHandle_t tx_task;
  line_rx_t uart_rx;
#if USB_JTAG
  line_rx_t usb_rx;
#endif
  uint8_t frame[RAW_MAX];            // RX task only: the decoded candidate
  uint8_t raw[RAW_MAX];              // TX task only: [frame][crc16]
  uint8_t line[1u + COBS_MAX + 1u];  // TX task only: a 0x00 before it, so a text line before ends there; one after
} port_t;

static port_t s_console;
#if CONFIG_SYS_UART_LINK
static port_t s_link;
#endif

/* ----------------------------------------------------------------------------
 * The line: COBS-encoded [frame][crc16], between 0x00 bytes
 * ------------------------------------------------------------------------- */

// CRC16-CCITT (poly 0x1021, init 0xFFFF, no reflection, no final xor).
static uint16_t crc16(const uint8_t* data, uint32_t len) {
  uint16_t crc = 0xFFFFu;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

// COBS: every 0x00 of `in` becomes the distance to the next one; `out` holds no 0x00. Returns the bytes written.
static uint32_t cobs_encode(const uint8_t* in, uint32_t len, uint8_t* out) {
  uint32_t code_at = 0;
  uint32_t pos = 1;
  uint8_t code = 1;
  for (uint32_t i = 0; i < len; i++) {
    if (in[i] != 0) {
      out[pos++] = in[i];
      code++;
    }
    if (in[i] == 0 || code == 0xFFu) {
      out[code_at] = code;
      code_at = pos++;
      code = 1;
    }
  }
  out[code_at] = code;
  return pos;
}

// The reverse of cobs_encode, into another buffer. Returns the bytes decoded; 0 when `in` is no COBS.
static uint32_t cobs_decode(const uint8_t* in, uint32_t len, uint8_t* out, uint32_t cap) {
  uint32_t pos = 0;
  uint32_t i = 0;
  while (i < len) {
    const uint8_t code = in[i++];
    if (code == 0 || i + code - 1u > len) {
      return 0;
    }
    for (uint8_t k = 1; k < code; k++) {
      if (pos >= cap) {
        return 0;
      }
      out[pos++] = in[i++];
    }
    if (code != 0xFFu && i < len) {
      if (pos >= cap) {
        return 0;
      }
      out[pos++] = 0;
    }
  }
  return pos;
}

/* ----------------------------------------------------------------------------
 * TX: the provider's send only queues the frame; the TX task encodes and writes it, the urgent queue first
 * ------------------------------------------------------------------------- */

/* One encoded frame to the port's outputs. The console's UART at its baud rate is far slower than USB: a frame that
 * does not fit its TX buffer now is not written there (its host misses it, as a dropped frame), so a USB host is never
 * held to the UART's pace. The link's TX task waits for room: nothing else waits on it. */
static void write_out(port_t* p, const uint8_t* data, uint32_t len) {
  size_t room = 0;
  if (!p->console || (uart_get_tx_buffer_free_size(p->uart, &room) == ESP_OK && room >= len)) {
    uart_write_bytes(p->uart, data, len);
  }
#if USB_JTAG
  // The driver takes no write longer than its buffer: a frame goes in pieces of half of it, until the host stops reading.
  for (uint32_t done = 0; p->console && done < len && usb_serial_jtag_is_connected();) {
    const uint32_t piece = len - done < USB_JTAG_TX_BUF / 2u ? len - done : USB_JTAG_TX_BUF / 2u;
    const int n = usb_serial_jtag_write_bytes(data + done, piece, MSEC(CONFIG_SYS_UART_SEND_WAIT_MS));
    if (n <= 0) {
      break;
    }
    done += (uint32_t)n;
  }
#endif
}

static void write_frame(port_t* p, const uint8_t* frame, uint32_t len) {
  memcpy(p->raw, frame, len);
  const uint16_t crc = crc16(frame, len);
  p->raw[len] = (uint8_t)crc;
  p->raw[len + 1] = (uint8_t)(crc >> 8);
  p->line[0] = 0;
  const uint32_t n = cobs_encode(p->raw, len + CRC_BYTES, p->line + 1);
  p->line[1 + n] = 0;
  write_out(p, p->line, n + 2u);
}

static void tx_task(void* arg) {
  port_t* p = arg;
  while (1) {
    size_t size = 0;
    uint8_t* item = xRingbufferReceive(p->urgent_rb, &size, NO_WAIT);
    RingbufHandle_t rb = p->urgent_rb;
    if (item == NULL) {
      item = xRingbufferReceive(p->tx_rb, &size, NO_WAIT);
      rb = p->tx_rb;
    }
    if (item != NULL) {
      write_frame(p, item, (uint32_t)size);
      vRingbufferReturnItem(rb, item);
      continue;
    }
    uint32_t bits = 0;
    U_NOTIFY_AWAIT(WAIT_FOREVER, &bits);
  }
}

static err_h provider_send(sys_data_provider_t* self, const uint8_t* data, uint32_t len, uint8_t flags) {
  port_t* p = (port_t*)(void*)((uint8_t*)self - offsetof(port_t, provider));
  RingbufHandle_t rb = (flags & SYS_DATA_PROVIDER_SEND_URGENT) ? p->urgent_rb : p->tx_rb;
  const TickType_t wait = (flags & SYS_DATA_PROVIDER_SEND_NOWAIT) ? NO_WAIT : MSEC(CONFIG_SYS_UART_SEND_WAIT_MS);
  ERR_CHECK(xRingbufferSend(rb, data, len, wait) == pdTRUE, ERR_PROV_BUSY, .link = self->id.link,
            .channel = self->id.channel);
  xTaskNotify(p->tx_task, NOTIFY_FRAME, eSetBits);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * RX: bytes -> the bytes up to each 0x00 -> COBS, CRC16 -> one frame to the provider's user
 * ------------------------------------------------------------------------- */

/* One candidate: decoded, its CRC checked, delivered whole. Anything else (the ROM's text, a panic, typing, a line
 * hit by noise) fails and is dropped without a word: the host's tool shows those bytes as text. */
static void rx_candidate(port_t* p, const uint8_t* data, uint32_t len) {
  const uint32_t n = cobs_decode(data, len, p->frame, sizeof(p->frame));
  if (n <= CRC_BYTES) {
    return;
  }
  const uint16_t crc = (uint16_t)(p->frame[n - 2] | (p->frame[n - 1] << 8));
  if (crc16(p->frame, n - CRC_BYTES) != crc) {
    return;
  }
  sys_data_provider_deliver(&p->provider, p->frame, n - CRC_BYTES);
}

static void rx_bytes(port_t* p, line_rx_t* rx, const uint8_t* data, int count) {
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
  port_t* p = arg;
  uint8_t chunk[READ_CHUNK];
  while (1) {
    rx_bytes(p, &p->uart_rx, chunk, uart_read_bytes(p->uart, chunk, sizeof(chunk), MSEC(READ_WAIT_MS)));
#if USB_JTAG
    if (p->console) {
      rx_bytes(p, &p->usb_rx, chunk, usb_serial_jtag_read_bytes(chunk, sizeof(chunk), MSEC(READ_WAIT_MS)));
    }
#endif
  }
}

/* ----------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------- */

// The port's fields, before its tasks run (they read the rings) and before the provider is added.
static void prepare_port(port_t* p, uart_port_t uart, bool console, sys_data_provider_id_t id, RingbufHandle_t tx_rb,
                         RingbufHandle_t urgent_rb) {
  p->uart = uart;
  p->console = console;
  p->tx_rb = tx_rb;
  p->urgent_rb = urgent_rb;
  p->provider = (sys_data_provider_t){
      .send = provider_send,
      .max_frame = CMD_FRAME_MAX,
      .id = id,
      .state = SYS_DATA_PROVIDER_STATE_FRAMED | SYS_DATA_PROVIDER_STATE_WIRED,
  };
}

#if CONFIG_SYS_UART_LINK
// The link's UART: 8N1 at the link's rate on its own pins, with RTS / CTS when both are given.
static err_h link_uart_init(void) {
  const bool flow = CONFIG_SYS_UART_LINK_RTS_PIN >= 0 && CONFIG_SYS_UART_LINK_CTS_PIN >= 0;
  const uart_config_t cfg = {
      .baud_rate = CONFIG_SYS_UART_LINK_BAUD,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = flow ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 100,
      .source_clk = UART_SCLK_DEFAULT,
  };
  int flags = 0;
#if CONFIG_UART_ISR_IN_IRAM
  flags |= ESP_INTR_FLAG_IRAM;  // a flash write (NVS) stops the cache for milliseconds: the FIFO must still be served
#endif
  ERR_TRY(ERR_ESP(uart_driver_install(CONFIG_SYS_UART_LINK_PORT, CONFIG_SYS_UART_LINK_RX_BUF, UART_TX_BUF, 0, NULL,
                                      flags)));
  ERR_TRY(ERR_ESP(uart_param_config(CONFIG_SYS_UART_LINK_PORT, &cfg)));
  return ERR_ESP(uart_set_pin(CONFIG_SYS_UART_LINK_PORT, CONFIG_SYS_UART_LINK_TX_PIN, CONFIG_SYS_UART_LINK_RX_PIN,
                              flow ? CONFIG_SYS_UART_LINK_RTS_PIN : UART_PIN_NO_CHANGE,
                              flow ? CONFIG_SYS_UART_LINK_CTS_PIN : UART_PIN_NO_CHANGE));
}
#endif

err_h sys_uart_init(void) {
  if (!uart_is_driver_installed(CONSOLE_PORT)) {
    ERR_TRY_ESP(uart_driver_install(CONSOLE_PORT, CONFIG_SYS_UART_DRIVER_RX_BUF, UART_TX_BUF, 0, NULL, 0));
    fflush(stdout);
    uart_vfs_dev_use_driver(CONSOLE_PORT);
  }
#if USB_JTAG
  if (!usb_serial_jtag_is_driver_installed()) {
    usb_serial_jtag_driver_config_t cfg = {.tx_buffer_size = USB_JTAG_TX_BUF,
                                           .rx_buffer_size = CONFIG_SYS_UART_DRIVER_RX_BUF};
    ERR_TRY_ESP(usb_serial_jtag_driver_install(&cfg));
  }
#endif
  prepare_port(&s_console, CONSOLE_PORT, true, SYS_UART_PROVIDER, sys_uart_tx_rb, sys_uart_urgent_rb);
  if (sys_uart_tx_task_h == NULL) {
    U_TASK_START(sys_uart_tx_task_h, tx_task, &s_console, CONFIG_SYS_UART_TX_TASK_PRIO);
  }
  s_console.tx_task = sys_uart_tx_task_h;
  ERR_TRY(sys_data_provider_add(&s_console.provider));
  if (sys_uart_rx_task_h == NULL) {
    U_TASK_START(sys_uart_rx_task_h, rx_task, &s_console, CONFIG_SYS_UART_TASK_PRIO);
  }
#if CONFIG_SYS_UART_LINK
  if (!uart_is_driver_installed(CONFIG_SYS_UART_LINK_PORT)) {
    ERR_TRY(link_uart_init());
  }
  prepare_port(&s_link, CONFIG_SYS_UART_LINK_PORT, false, SYS_UART_LINK_PROVIDER, sys_uart_link_tx_rb,
               sys_uart_link_urgent_rb);
  if (sys_uart_link_tx_task_h == NULL) {
    U_TASK_START(sys_uart_link_tx_task_h, tx_task, &s_link, CONFIG_SYS_UART_TX_TASK_PRIO);
  }
  s_link.tx_task = sys_uart_link_tx_task_h;
  ERR_TRY(sys_data_provider_add(&s_link.provider));
  if (sys_uart_link_rx_task_h == NULL) {
    U_TASK_START(sys_uart_link_rx_task_h, rx_task, &s_link, CONFIG_SYS_UART_TASK_PRIO);
  }
#endif
  return NULL;
}
