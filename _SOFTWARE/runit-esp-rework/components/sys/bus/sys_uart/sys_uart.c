#include "sys_uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "cmd_table.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "sdkconfig.h"
#include "sys_data_provider_driver.h"
#include "sys_uart_codec.h"
#include "utils.h"

#define USB_JTAG (CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
#if USB_JTAG
#include "driver/usb_serial_jtag.h"
#endif

/* ERR_MOD_SYS_UART and MODULE_ID_UART (0x13) are defined in sys_uart_contract.c. */

/* Size of one item in a no-split ring: data rounded up to 4 + 8 B header (ESP-IDF ringbuf implementation). */
#define ITEM_BYTES(len) ((((len) + 3u) & ~3u) + 8u)

#define CONSOLE_PORT    CONFIG_ESP_CONSOLE_UART_NUM
#define USB_JTAG_TX_BUF 512u                        // the driver's TX buffer: a frame in a few writes
#define USB_JTAG_PIECE  (USB_JTAG_TX_BUF / 2u)      // the longest write the driver takes at once
#define UART_TX_BUF     4096u                       // the console's UART TX buffer: a burst waits there, not here
#define CRC_BYTES       SYS_UART_CRC_BYTES          // CRC16-CCITT after the frame, little-endian
#define RAW_MAX         (CMD_FRAME_MAX + CRC_BYTES) // [frame][crc16]
#define COBS_MAX        SYS_UART_COBS_MAX(RAW_MAX)  // COBS adds at most one byte in 254 plus code bytes
#define READ_CHUNK      64u
#define READ_WAIT_MS    10u
#define URGENT_QUEUE    (ITEM_BYTES(RAW_MAX) * 2u)  // two urgent frames waiting at most

_Static_assert(CONFIG_SYS_UART_TX_QUEUE % 4 == 0, "SYS_UART_TX_QUEUE must be a multiple of 4");

U_TASK_DEFINE(sys_uart_rx_task_h, CONFIG_SYS_UART_TASK_STACK);
U_TASK_DEFINE(sys_uart_tx_task_h, CONFIG_SYS_UART_TX_TASK_STACK);
U_RINGBUFFER_DEFINE(sys_uart_tx_rb, CONFIG_SYS_UART_TX_QUEUE, RINGBUF_TYPE_NOSPLIT);
U_RINGBUFFER_DEFINE(sys_uart_urgent_rb, URGENT_QUEUE, RINGBUF_TYPE_NOSPLIT);

typedef struct {
  uint8_t buf[COBS_MAX];
  uint32_t len;
  bool overflow;  // longer than any frame: dropped at its 0x00
} line_rx_t;

typedef struct {
  sys_data_provider_t* provider;  // slot-owned handle; set by console_create()
  uart_port_t uart;
  RingbufHandle_t tx_rb;
  RingbufHandle_t urgent_rb;
  TaskHandle_t tx_task;
  line_rx_t uart_rx;
#if USB_JTAG
  line_rx_t usb_rx;
#endif
  uint8_t line[1u + COBS_MAX + 1u];  // TX task only: a 0x00 before it, so a text line before ends there; one after
} console_port_t;

static console_port_t s_console;

/* ----------------------------------------------------------------------------
 * TX: the provider's send only queues the frame; the TX task encodes and writes it, the urgent queue first
 * ------------------------------------------------------------------------- */

#if USB_JTAG
// A frame to the USB-Serial-JTAG in pieces the driver takes, until the host stops reading.
static void write_usb(const uint8_t* data, uint32_t len) {
  uint32_t done = 0;
  while (done < len && usb_serial_jtag_is_connected()) {
    const uint32_t piece = len - done < USB_JTAG_PIECE ? len - done : USB_JTAG_PIECE;
    const int n = usb_serial_jtag_write_bytes(data + done, piece, MSEC(CONFIG_SYS_UART_SEND_WAIT_MS));
    if (n <= 0) {
      return;
    }
    done += (uint32_t)n;
  }
}
#endif

/* The console's UART at its baud rate is far slower than USB: a frame that does not fit its TX buffer now is not written
 * there (its host misses it, as a dropped frame), so a USB host is never held to the UART's pace. */
static void write_out(const console_port_t* p, const uint8_t* data, uint32_t len) {
  size_t room = 0;
  if (uart_get_tx_buffer_free_size(p->uart, &room) == ESP_OK && room >= len) {
    uart_write_bytes(p->uart, data, len);
  }
#if USB_JTAG
  write_usb(data, len);
#endif
}

// One queued item, [frame][crc16], COBS-encoded between two 0x00 to the console outputs.
static void write_frame(console_port_t* p, const uint8_t* item, uint32_t len) {
  p->line[0] = 0;
  const uint32_t n = sys_uart_cobs_encode(item, len, p->line + 1);
  p->line[1 + n] = 0;
  write_out(p, p->line, n + 2u);
}

// The next queued item, the urgent ring first; `rb` is the ring it came from. NULL when both are empty.
static uint8_t* next_item(const console_port_t* p, RingbufHandle_t* rb, size_t* size) {
  *rb = p->urgent_rb;
  uint8_t* item = xRingbufferReceive(*rb, size, NO_WAIT);
  if (item == NULL) {
    *rb = p->tx_rb;
    item = xRingbufferReceive(*rb, size, NO_WAIT);
  }
  return item;
}

/* Writes what is queued, then sleeps until the next send notifies it. A send between the empty check and the wait
 * leaves its notification pending, so the wait returns at once. */
static void tx_task(void* arg) {
  console_port_t* p = arg;
  while (true) {
    RingbufHandle_t rb = NULL;
    size_t size = 0;
    uint8_t* item = next_item(p, &rb, &size);
    if (item == NULL) {
      (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    write_frame(p, item, (uint32_t)size);
    vRingbufferReturnItem(rb, item);
  }
}

// Bytes occupied in a no-split ring (between pucFree and pucAcquire, accounting for wrap and full-vs-empty).
// xRingbufferGetCurFreeSize() cannot be used here: on a NOSPLIT ring it caps free size at cap / 2 - 8 (xMaxItemSize).
static uint32_t ring_used(RingbufHandle_t rb, uint32_t cap) {
  UBaseType_t free_off = 0, acq_off = 0, items = 0;
  vRingbufferGetInfo(rb, &free_off, NULL, NULL, &acq_off, &items);
  if (acq_off > free_off) {
    return (uint32_t)(acq_off - free_off);
  }
  if (acq_off < free_off) {
    return cap - (uint32_t)(free_off - acq_off);
  }
  return items > 0 ? cap : 0u;
}

/* Queues [head][body][crc16] as one item; `sys_uart_tx_task` encodes and writes it.
 * Note on `SPARE`: `ring_used` is a snapshot before acquiring space, so concurrent senders can overshoot by an item. */
static err_h provider_send(sys_data_provider_t* self, cmd_in_t frame, uint8_t flags) {
  console_port_t* p = self->driver;
  const bool urgent = (flags & CMD_SEND_URGENT) != 0;
  RingbufHandle_t rb = urgent ? p->urgent_rb : p->tx_rb;
  const uint32_t cap = urgent ? URGENT_QUEUE : CONFIG_SYS_UART_TX_QUEUE;
  const uint32_t used = ring_used(rb, cap);  // a snapshot: another send may come between
  ERR_CHECK(!sys_data_provider_spare_refused(flags, used, ITEM_BYTES(frame.len + CRC_BYTES), cap), ERR_PROV_BUSY,
            .provider = self->id);
  const TickType_t wait = (flags & CMD_SEND_NOWAIT) ? NO_WAIT : MSEC(CONFIG_SYS_UART_SEND_WAIT_MS);
  uint8_t* item = NULL;
  ERR_CHECK(xRingbufferSendAcquire(rb, (void**)&item, frame.len + CRC_BYTES, wait) == pdTRUE, ERR_PROV_BUSY,
            .provider = self->id);
  memcpy(item, frame.data, frame.len);
  const uint16_t crc = sys_uart_crc16(SYS_UART_CRC16_INIT, frame.data, frame.len);
  item[frame.len] = (uint8_t)crc;
  item[frame.len + 1] = (uint8_t)(crc >> 8);
  xRingbufferSendComplete(rb, item);
  xTaskNotifyGive(p->tx_task);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * RX: bytes -> the bytes up to each 0x00 -> COBS, CRC16 -> one frame to the provider's user
 * ------------------------------------------------------------------------- */

/* One candidate: decoded, its CRC checked, delivered whole. Anything else (the ROM's text, a panic, typing, a line
 * hit by noise) fails and is dropped without a word: the host's tool shows those bytes as text. */
static void rx_candidate(console_port_t* p, uint8_t* buf, uint32_t len) {
  const uint32_t n = sys_uart_cobs_decode(buf, len);
  if (n <= CRC_BYTES) {
    return;
  }
  const uint16_t crc = (uint16_t)(buf[n - 2] | (buf[n - 1] << 8));
  if (sys_uart_crc16(SYS_UART_CRC16_INIT, buf, n - CRC_BYTES) != crc) {
    return;
  }
  sys_data_provider_deliver(p->provider, buf, n - CRC_BYTES);
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

static err_h console_create(sys_data_provider_t* p, void* ctx, const void* cfg) {
  (void)ctx;
  (void)cfg;
  ERR_CHECK(s_console.provider == NULL, ERR_PROV_IN_USE, .provider = p->id);
  s_console.provider = p;
  p->driver = &s_console;
  p->send = provider_send;
  p->state |= SYS_DATA_PROVIDER_STATE_WIRED | SYS_DATA_PROVIDER_STATE_FRAMED | SYS_DATA_PROVIDER_STATE_UP;
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

/* ----------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------- */

err_h sys_uart_init(uint8_t console_provider) {
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
  s_console.uart = CONSOLE_PORT;
  s_console.tx_rb = sys_uart_tx_rb;
  s_console.urgent_rb = sys_uart_urgent_rb;
  if (sys_uart_tx_task_h == NULL) {
    U_TASK_START(sys_uart_tx_task_h, tx_task, &s_console, CONFIG_SYS_UART_TX_TASK_PRIO);
  }
  s_console.tx_task = sys_uart_tx_task_h;
  const uint8_t create_cfg[SYS_DATA_PROVIDER_CREATE_HDR] = {
      SYS_DATA_PROVIDER_TYPE_CONSOLE,
      SYS_DATA_PROVIDER_CREATE_SYSTEM,
  };
  ERR_TRY(sys_data_provider_write(console_provider, CONTRACT_PROVIDER_OP_CREATE, create_cfg, sizeof(create_cfg)));
  if (sys_uart_rx_task_h == NULL) {
    U_TASK_START(sys_uart_rx_task_h, rx_task, &s_console, CONFIG_SYS_UART_TASK_PRIO);
  }
  return NULL;
}
