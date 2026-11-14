#include "sys_uart.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_private/esp_gpio_reserve.h"
#include "sdkconfig.h"
#include "sys_data_provider_driver.h"
#include "utils.h"

#define USB_JTAG (CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
#if USB_JTAG
#include "driver/usb_serial_jtag.h"
#endif

ERR_MODULE_DEFINE(ERR_MOD_SYS_UART, "sys_uart", ERR_SYS_UART_MAP)
MODULE_ID_DEFINE(UART, 0x13)

// What a queued item takes in a no-split ring buffer: its bytes rounded to 4, and the ring's 8-byte header.
#define ITEM_BYTES(len) ((((len) + 3u) & ~3u) + 8u)

#define CONSOLE_PORT    CONFIG_ESP_CONSOLE_UART_NUM
#define LINE_BAUD       CONFIG_ESP_CONSOLE_UART_BAUDRATE  // the link's rate: the other board's console, the same firmware
#define USB_JTAG_TX_BUF 512u                             // the driver's TX buffer: a frame in a few writes
#define USB_JTAG_PIECE  (USB_JTAG_TX_BUF / 2u)           // the longest write the driver takes at once
#define UART_TX_BUF     4096u                            // the console's UART TX buffer: a burst waits there, not here
#define CRC_BYTES       2u                               // CRC16-CCITT after the frame, little-endian
#define RAW_MAX         (CMD_FRAME_MAX + CRC_BYTES)      // [frame][crc16]
#define COBS_MAX        (RAW_MAX + RAW_MAX / 254u + 1u)  // COBS adds at most one byte in 254, plus its first
#define READ_CHUNK      64u
#define READ_WAIT_MS    10u                              // the RX task's read: how often it looks at `paused`
#define PARK_WAIT_MS    2000u                            // how long lending the link waits for each of its tasks
#define URGENT_QUEUE    (ITEM_BYTES(RAW_MAX) * 2u)       // two urgent frames waiting at most

_Static_assert(CONFIG_SYS_UART_TX_QUEUE % 4 == 0, "SYS_UART_TX_QUEUE must be a multiple of 4");
_Static_assert(CONFIG_SYS_UART_TX_QUEUE >= 2u * ITEM_BYTES(RAW_MAX), "a whole frame fits the queue");
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

/* A UART of the board as a provider: the console's, or the link to another board. Both are framed; the link can be
 * lent as a DIRECT port: its tasks are parked (never deleted) and its owner writes and reads on its own task. Their
 * state, tasks and rings are static. The console also writes to and reads from the USB-Serial-JTAG. */
typedef struct {
  sys_data_provider_t provider;  // first: the driver's send gets a pointer to it
  uart_port_t uart;
  uint8_t mode;           // sys_uart_mode_e: FRAMED, or DIRECT while the link is lent
  bool console;          // a frame that does not fit the UART's TX buffer is dropped there, never waited for
  volatile bool paused;  // the link lent: its tasks touch neither the UART nor the rings
  RingbufHandle_t tx_rb;
  RingbufHandle_t urgent_rb;
  TaskHandle_t tx_task;
  TaskHandle_t rx_task;
  TaskHandle_t lender;  // who lends the link: told by each task as it parks
  line_rx_t uart_rx;
#if USB_JTAG
  line_rx_t usb_rx;
#endif
  uint8_t line[1u + COBS_MAX + 1u];  // TX task only: a 0x00 before it, so a text line before ends there; one after
} port_t;

_Static_assert(offsetof(port_t, provider) == 0, "the provider's send casts its pointer back to the port");

static port_t s_console;
#if CONFIG_SYS_UART_LINK
static port_t s_link;
#endif

/* ----------------------------------------------------------------------------
 * The line: COBS-encoded [frame][crc16], between 0x00 bytes
 * ------------------------------------------------------------------------- */

// CRC16-CCITT (poly 0x1021, init 0xFFFF, no reflection, no final xor), carried on from `crc` over `data`.
static uint16_t crc16(uint16_t crc, const uint8_t* data, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

#define CRC16_INIT 0xFFFFu

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

// The reverse of cobs_encode, in place: a byte is written only where one was read already (the decoded bytes are never
// more). Returns the bytes decoded; 0 when `buf` is no COBS.
static uint32_t cobs_decode(uint8_t* buf, uint32_t len) {
  uint32_t pos = 0;
  uint32_t i = 0;
  while (i < len) {
    const uint8_t code = buf[i++];
    if (code == 0 || i + code - 1u > len) {
      return 0;
    }
    for (uint8_t k = 1; k < code; k++) {
      buf[pos++] = buf[i++];
    }
    if (code != 0xFFu && i < len) {
      buf[pos++] = 0;
    }
  }
  return pos;
}

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

/* Bytes to the port's outputs. The link's TX task waits for room in the FIFO: nothing else waits on it. The console's
 * UART at its baud rate is far slower than USB: a frame that does not fit its TX buffer now is not written there (its
 * host misses it, as a dropped frame), so a USB host is never held to the UART's pace. */
static void write_out(port_t* p, const uint8_t* data, uint32_t len) {
  if (!p->console) {
    uart_write_bytes(p->uart, data, len);
    return;
  }
  size_t room = 0;
  if (uart_get_tx_buffer_free_size(p->uart, &room) == ESP_OK && room >= len) {
    uart_write_bytes(p->uart, data, len);
  }
#if USB_JTAG
  write_usb(data, len);
#endif
}

// One queued item, [frame][crc16], COBS-encoded between two 0x00 to the port's outputs.
static void write_frame(port_t* p, const uint8_t* item, uint32_t len) {
  p->line[0] = 0;
  const uint32_t n = cobs_encode(item, len, p->line + 1);
  p->line[1 + n] = 0;
  write_out(p, p->line, n + 2u);
}

// The next queued item, the urgent ring first; `rb` is the ring it came from. NULL when both are empty.
static uint8_t* next_item(port_t* p, RingbufHandle_t* rb, size_t* size) {
  *rb = p->urgent_rb;
  uint8_t* item = xRingbufferReceive(*rb, size, NO_WAIT);
  if (item == NULL) {
    *rb = p->tx_rb;
    item = xRingbufferReceive(*rb, size, NO_WAIT);
  }
  return item;
}

// The link lent: the task tells the lender it is parked and sleeps until the link is taken back.
static void park(port_t* p) {
  xTaskNotifyGive(p->lender);
  while (__atomic_load_n(&p->paused, __ATOMIC_ACQUIRE)) {
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
}

/* Writes what is queued, then sleeps until the next send notifies it. A send between the empty check and the wait
 * leaves its notification pending, so the wait returns at once. */
static void tx_task(void* arg) {
  port_t* p = arg;
  while (true) {
    if (__atomic_load_n(&p->paused, __ATOMIC_ACQUIRE)) {
      park(p);
      continue;
    }
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

/* Queues [head][body][crc16] as one item: the CRC runs over both parts, so the TX task encodes the item as it is. A
 * SPARE frame is refused when it would fill its ring past half (sys_data_provider_spare_refused). */
static err_h provider_send(sys_data_provider_t* self, const uint8_t* head, uint32_t head_len, const uint8_t* body,
                           uint32_t body_len, uint8_t flags) {
  port_t* p = (port_t*)self;
  // The lent link's provider is down, so the registry refuses its sends; one that passed before it went down stops here.
  ERR_CHECK(p->mode != SYS_UART_MODE_DIRECT, ERR_PROV_DOWN, .provider = self->id);
  const bool urgent = (flags & SYS_DATA_PROVIDER_SEND_URGENT) != 0;
  RingbufHandle_t rb = urgent ? p->urgent_rb : p->tx_rb;
  const uint32_t len = head_len + body_len;
  const uint32_t cap = urgent ? URGENT_QUEUE : CONFIG_SYS_UART_TX_QUEUE;
  const uint32_t used = cap - (uint32_t)xRingbufferGetCurFreeSize(rb);  // a snapshot: another send may come between
  ERR_CHECK(!sys_data_provider_spare_refused(flags, used, ITEM_BYTES(len + CRC_BYTES), cap), ERR_PROV_BUSY,
            .provider = self->id);
  const TickType_t wait = (flags & SYS_DATA_PROVIDER_SEND_NOWAIT) ? NO_WAIT : MSEC(CONFIG_SYS_UART_SEND_WAIT_MS);
  uint8_t* item = NULL;
  ERR_CHECK(xRingbufferSendAcquire(rb, (void**)&item, len + CRC_BYTES, wait) == pdTRUE, ERR_PROV_BUSY,
            .provider = self->id);
  memcpy(item, head, head_len);
  memcpy(item + head_len, body, body_len);
  const uint16_t crc = crc16(crc16(CRC16_INIT, head, head_len), body, body_len);
  item[len] = (uint8_t)crc;
  item[len + 1] = (uint8_t)(crc >> 8);
  xRingbufferSendComplete(rb, item);
  xTaskNotifyGive(p->tx_task);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * RX: bytes -> the bytes up to each 0x00 -> COBS, CRC16 -> one frame to the provider's user
 * ------------------------------------------------------------------------- */

/* One candidate: decoded, its CRC checked, delivered whole. Anything else (the ROM's text, a panic, typing, a line
 * hit by noise) fails and is dropped without a word: the host's tool shows those bytes as text. */
static void rx_candidate(port_t* p, uint8_t* buf, uint32_t len) {
  const uint32_t n = cobs_decode(buf, len);  // in its own buffer: the next candidate starts after the user has it
  if (n <= CRC_BYTES) {
    return;
  }
  const uint16_t crc = (uint16_t)(buf[n - 2] | (buf[n - 1] << 8));
  if (crc16(CRC16_INIT, buf, n - CRC_BYTES) != crc) {
    return;
  }
  sys_data_provider_deliver(&p->provider, buf, n - CRC_BYTES);
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
  while (true) {
    if (__atomic_load_n(&p->paused, __ATOMIC_ACQUIRE)) {
      park(p);
      continue;
    }
    rx_bytes(p, &p->uart_rx, chunk, uart_read_bytes(p->uart, chunk, sizeof(chunk), MSEC(READ_WAIT_MS)));
#if USB_JTAG
    if (p->console) {
      rx_bytes(p, &p->usb_rx, chunk, usb_serial_jtag_read_bytes(chunk, sizeof(chunk), MSEC(READ_WAIT_MS)));
    }
#endif
  }
}

/* ----------------------------------------------------------------------------
 * A DIRECT port: the caller's task writes and reads (the link while it is lent)
 * ------------------------------------------------------------------------- */

// The UART of the DIRECT port with this provider id. Its owner holds the provider, so it stays meanwhile.
static bool direct_port(uint8_t provider, uart_port_t* uart) {
#if CONFIG_SYS_UART_LINK
  if (s_link.provider.id == provider && s_link.mode == SYS_UART_MODE_DIRECT) {
    *uart = s_link.uart;
    return true;
  }
#endif
  (void)provider;
  (void)uart;
  return false;
}

err_h sys_uart_write(uint8_t provider, const uint8_t* data, uint32_t len, uint32_t wait_ms) {
  ERR_CHECK(data != NULL && len > 0, ERR_UART_BAD_ARG);
  uart_port_t uart;
  ERR_CHECK(direct_port(provider, &uart), ERR_UART_NOT_DIRECT, .provider = provider);
  uart_write_bytes(uart, data, len);
  ERR_CHECK(uart_wait_tx_done(uart, MSEC(wait_ms)) == ESP_OK, ERR_UART_TIMEOUT, .provider = provider);
  return NULL;
}

err_h sys_uart_read(uint8_t provider, uint8_t* data, uint32_t cap, uint32_t* len, uint32_t wait_ms, uint32_t idle_ms) {
  ERR_CHECK(data != NULL && cap > 0 && len != NULL, ERR_UART_BAD_ARG);
  uart_port_t uart;
  ERR_CHECK(direct_port(provider, &uart), ERR_UART_NOT_DIRECT, .provider = provider);
  if (idle_ms == 0) {
    const int n = uart_read_bytes(uart, data, cap, MSEC(wait_ms));
    *len = n > 0 ? (uint32_t)n : 0;
    return NULL;
  }
  // The first byte may take long; after it a read that brings nothing is the pause.
  uint32_t total = uart_read_bytes(uart, data, 1, MSEC(wait_ms)) > 0 ? 1u : 0u;
  while (total > 0 && total < cap) {
    const int n = uart_read_bytes(uart, data + total, cap - total, MSEC(idle_ms));
    if (n <= 0) {
      break;
    }
    total += (uint32_t)n;
  }
  *len = total;
  return NULL;
}

err_h sys_uart_flush_input(uint8_t provider) {
  uart_port_t uart;
  ERR_CHECK(direct_port(provider, &uart), ERR_UART_NOT_DIRECT, .provider = provider);
  return ERR_ESP(uart_flush_input(uart));
}

err_h sys_uart_set_baud(uint8_t provider, uint32_t baud) {
  ERR_CHECK(baud >= SYS_UART_BAUD_MIN && baud <= SYS_UART_BAUD_MAX, ERR_UART_BAD_BAUD, .baud = baud);
  uart_port_t uart;
  ERR_CHECK(direct_port(provider, &uart), ERR_UART_NOT_DIRECT, .provider = provider);
  ERR_TRY_ESP(uart_wait_tx_done(uart, MSEC(CONFIG_SYS_UART_SEND_WAIT_MS)));
  return ERR_ESP(uart_set_baudrate(uart, baud));
}

/* ----------------------------------------------------------------------------
 * The link: made at init, lent as a DIRECT port (sys_flasher: the ROM bootloader of the board on it)
 * ------------------------------------------------------------------------- */

#if CONFIG_SYS_UART_LINK
/* Parks the link's two tasks: each tells the lender as it parks (the RX task within READ_WAIT_MS, the TX task after
 * the frame it writes). The lender's notification is used, so it is not one that waits on anything else. */
static err_h park_tasks(port_t* p) {
  p->lender = xTaskGetCurrentTaskHandle();
  (void)ulTaskNotifyTake(pdTRUE, NO_WAIT);
  __atomic_store_n(&p->paused, true, __ATOMIC_RELEASE);
  xTaskNotifyGive(p->tx_task);  // asleep in its wait
  uint32_t parked = 0;
  while (parked < 2 && ulTaskNotifyTake(pdFALSE, MSEC(PARK_WAIT_MS)) > 0) {
    parked++;
  }
  ERR_CHECK(parked == 2, ERR_UART_TIMEOUT, .provider = p->provider.id);  // a TX task held by a line that never frees
  return NULL;
}

static void wake_tasks(port_t* p) {
  __atomic_store_n(&p->paused, false, __ATOMIC_RELEASE);
  xTaskNotifyGive(p->tx_task);
  xTaskNotifyGive(p->rx_task);
}

// Drops what is still queued to a port whose tasks are stopped.
static void drop_queued(port_t* p) {
  RingbufHandle_t rb = NULL;
  size_t size = 0;
  for (uint8_t* item = next_item(p, &rb, &size); item != NULL; item = next_item(p, &rb, &size)) {
    vRingbufferReturnItem(rb, item);
  }
}

err_h sys_uart_link_direct(bool direct) {
  port_t* p = &s_link;
  ERR_CHECK(p->provider.send != NULL, ERR_UART_BAD_PORT, .port = CONFIG_SYS_UART_LINK_PORT);
  if (direct == (p->mode == SYS_UART_MODE_DIRECT)) {
    return NULL;
  }
  if (direct) {
    sys_data_provider_set_up(&p->provider, false);  // its session ends; sends are refused while it is lent
    err_h err = park_tasks(p);
    if (err != NULL) {
      wake_tasks(p);  // not lent: the tasks go on, the provider up again
      sys_data_provider_set_up(&p->provider, true);
      return err;
    }
    drop_queued(p);
    p->mode = SYS_UART_MODE_DIRECT;
    return NULL;
  }
  ERR_TRY_ESP(uart_set_baudrate(p->uart, LINE_BAUD));
  ERR_TRY_ESP(uart_flush_input(p->uart));
  p->uart_rx.len = 0;
  p->uart_rx.overflow = false;
  p->mode = SYS_UART_MODE_FRAMED;
  wake_tasks(p);
  sys_data_provider_set_up(&p->provider, true);  // its session says hello
  return NULL;
}

/* The link's UART: 8N1 at the console's rate on its Kconfig pins, with RTS / CTS when both are given, and those pins
 * reserved in ESP-IDF, so the GPIO device refuses them (ERR_DEV_PIN_LOCKED, no device's). No TX buffer: its TX task
 * writes to the FIFO and waits there, its queue is the only one. The ISR in IRAM when sdkconfig says so: a flash write
 * stops the cache for milliseconds, and the FIFO must still be served. */
static err_h link_init(uint8_t provider) {
#if CONFIG_SYS_UART_LINK_RTS_PIN >= 0 && CONFIG_SYS_UART_LINK_CTS_PIN >= 0
  const bool flow = true;
  const int rts = CONFIG_SYS_UART_LINK_RTS_PIN;
  const int cts = CONFIG_SYS_UART_LINK_CTS_PIN;
  const uint64_t flow_pins = BIT64(CONFIG_SYS_UART_LINK_RTS_PIN) | BIT64(CONFIG_SYS_UART_LINK_CTS_PIN);
#else
  const bool flow = false;
  const int rts = UART_PIN_NO_CHANGE;
  const int cts = UART_PIN_NO_CHANGE;
  const uint64_t flow_pins = 0;
#endif
  const uart_config_t cfg = {
      .baud_rate = LINE_BAUD,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = flow ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 100,
      .source_clk = UART_SCLK_DEFAULT,
  };
  int intr = 0;
#if CONFIG_UART_ISR_IN_IRAM
  intr |= ESP_INTR_FLAG_IRAM;
#endif
  esp_err_t rc = uart_driver_install(CONFIG_SYS_UART_LINK_PORT, CONFIG_SYS_UART_PORT_RX_BUF, 0, 0, NULL, intr);
  if (rc == ESP_OK) {
    rc = uart_param_config(CONFIG_SYS_UART_LINK_PORT, &cfg);
  }
  if (rc == ESP_OK) {
    rc = uart_set_pin(CONFIG_SYS_UART_LINK_PORT, CONFIG_SYS_UART_LINK_TX_PIN, CONFIG_SYS_UART_LINK_RX_PIN, rts, cts);
  }
  ERR_CHECK(rc == ESP_OK, ERR_UART_INIT, .port = CONFIG_SYS_UART_LINK_PORT);
  (void)esp_gpio_reserve(BIT64(CONFIG_SYS_UART_LINK_TX_PIN) | BIT64(CONFIG_SYS_UART_LINK_RX_PIN) | flow_pins);

  s_link.uart = CONFIG_SYS_UART_LINK_PORT;
  s_link.mode = SYS_UART_MODE_FRAMED;
  s_link.tx_rb = sys_uart_link_tx_rb;
  s_link.urgent_rb = sys_uart_link_urgent_rb;
  s_link.provider = (sys_data_provider_t){
      .send = provider_send,
      .id = provider,
      .link = SYS_DATA_PROVIDER_LINK_UART,
      .state = SYS_DATA_PROVIDER_STATE_SYSTEM | SYS_DATA_PROVIDER_STATE_WIRED | SYS_DATA_PROVIDER_STATE_FRAMED |
               SYS_DATA_PROVIDER_STATE_UP,
  };
  U_TASK_START(sys_uart_link_tx_task_h, tx_task, &s_link, CONFIG_SYS_UART_TX_TASK_PRIO);
  U_TASK_START(sys_uart_link_rx_task_h, rx_task, &s_link, CONFIG_SYS_UART_TASK_PRIO);
  s_link.tx_task = sys_uart_link_tx_task_h;
  s_link.rx_task = sys_uart_link_rx_task_h;
  return sys_data_provider_add(&s_link.provider);
}
#else
err_h sys_uart_link_direct(bool direct) {
  (void)direct;
  return ERR_NEW(ERR_UART_BAD_PORT, .port = UART_NUM_MAX);
}
#endif

/* ----------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------- */

err_h sys_uart_init(uint8_t console_provider, uint8_t link_provider) {
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
  s_console.mode = SYS_UART_MODE_FRAMED;
  s_console.console = true;
  s_console.tx_rb = sys_uart_tx_rb;
  s_console.urgent_rb = sys_uart_urgent_rb;
  s_console.provider = (sys_data_provider_t){
      .send = provider_send,
      .id = console_provider,
      .link = SYS_DATA_PROVIDER_LINK_UART,
      .state = SYS_DATA_PROVIDER_STATE_SYSTEM | SYS_DATA_PROVIDER_STATE_WIRED | SYS_DATA_PROVIDER_STATE_FRAMED |
               SYS_DATA_PROVIDER_STATE_UP,
  };
  if (sys_uart_tx_task_h == NULL) {
    U_TASK_START(sys_uart_tx_task_h, tx_task, &s_console, CONFIG_SYS_UART_TX_TASK_PRIO);
  }
  s_console.tx_task = sys_uart_tx_task_h;
  ERR_TRY(sys_data_provider_add(&s_console.provider));
  if (sys_uart_rx_task_h == NULL) {
    U_TASK_START(sys_uart_rx_task_h, rx_task, &s_console, CONFIG_SYS_UART_TASK_PRIO);
  }
#if CONFIG_SYS_UART_LINK
  ERR_TRY(link_init(link_provider));
#else
  (void)link_provider;
#endif
  return NULL;
}
