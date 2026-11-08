#include "sys_uart.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "sdkconfig.h"
#include "sys_data_provider_driver.h"
#include "sys_device.h"
#include "sys_uart_router.h"
#include "utils.h"

#define USB_JTAG (CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
#if USB_JTAG
#include "driver/usb_serial_jtag.h"
#endif

ERR_MODULE_DEFINE(ERR_MOD_SYS_UART, "sys_uart", ERR_SYS_UART_MAP)

// What a queued item takes in a no-split ring buffer: its bytes rounded to 4, and the ring's 8-byte header.
#define ITEM_BYTES(len) ((((len) + 3u) & ~3u) + 8u)

#define CONSOLE_PORT    CONFIG_ESP_CONSOLE_UART_NUM
#define USB_JTAG_TX_BUF 512u                             // the driver's TX buffer: a frame in a few writes
#define USB_JTAG_PIECE  (USB_JTAG_TX_BUF / 2u)           // the longest write the driver takes at once
#define UART_TX_BUF     4096u                            // the console's UART TX buffer: a burst waits there, not here
#define CRC_BYTES       2u                               // CRC16-CCITT after the frame, little-endian
#define RAW_MAX         (CMD_FRAME_MAX + CRC_BYTES)      // [frame][crc16]
#define COBS_MAX        (RAW_MAX + RAW_MAX / 254u + 1u)  // COBS adds at most one byte in 254, plus its first
#define RAW_RX_MAX      (CMD_DATA_MAX - 1u)              // a raw message: the longest payload of a stream (SYS_STREAM.MD)
#define READ_CHUNK      64u
#define READ_WAIT_MS    10u                              // a raw port's pause: a message ends after a read this long with no byte
#define STOP_WAIT_MS    2000u                            // how long removing a port waits for each of its tasks
#define URGENT_QUEUE    (ITEM_BYTES(RAW_MAX) * 2u)       // two urgent frames waiting at most

_Static_assert(CONFIG_SYS_UART_TX_QUEUE % 4 == 0, "SYS_UART_TX_QUEUE must be a multiple of 4");
_Static_assert(CONFIG_SYS_UART_TX_QUEUE >= 2u * ITEM_BYTES(RAW_MAX), "a whole frame fits the queue");
_Static_assert(RAW_RX_MAX <= COBS_MAX, "a raw message fits the candidate buffer");
#if CONFIG_SYS_UART_LINK
_Static_assert(CONFIG_SYS_UART_LINK_PORT != CONSOLE_PORT, "the link needs a UART of its own");
#endif

U_TASK_DEFINE(sys_uart_rx_task_h, CONFIG_SYS_UART_TASK_STACK);
U_TASK_DEFINE(sys_uart_tx_task_h, CONFIG_SYS_UART_TX_TASK_STACK);
U_RINGBUFFER_DEFINE(sys_uart_tx_rb, CONFIG_SYS_UART_TX_QUEUE, RINGBUF_TYPE_NOSPLIT);
U_RINGBUFFER_DEFINE(sys_uart_urgent_rb, URGENT_QUEUE, RINGBUF_TYPE_NOSPLIT);
U_MUTEX_DEFINE(sys_uart_lock);
U_MUTEX_DEFINE(sys_uart_transact_mutex);
U_BINARY_SEM_DEFINE(sys_uart_transact_sem);

// One candidate per input (UART, USB-Serial-JTAG): the bytes since the last 0x00; a raw port's message so far.
typedef struct {
  uint8_t buf[COBS_MAX];
  uint32_t len;
  bool overflow;  // longer than any frame: dropped at its 0x00
} line_rx_t;

/* A UART as a provider: the console's, the link to another board, or a port CREATE made. The console's tasks and
 * rings are static; the others' are made with the port and freed with it. The console also writes to and reads from the
 * USB-Serial-JTAG. */
typedef struct {
  sys_data_provider_t provider;  // first: the driver's send gets a pointer to it
  uart_port_t uart;
  uint8_t mode;  // sys_uart_mode_e
  bool console;  // the console: a frame that does not fit the UART's TX buffer is dropped there, never waited for
  bool made;     // made by CREATE (not the board's own): sys_uart_get() answers it
  bool installed;  // the UART driver is installed by this port
  volatile bool stop;     // removing the port: its tasks come out
  volatile bool tx_done;  // the TX task is out
  volatile bool rx_done;  // the RX task is out
  RingbufHandle_t tx_rb;
  RingbufHandle_t urgent_rb;
  TaskHandle_t tx_task;
  TaskHandle_t rx_task;
  TaskHandle_t closer;  // who removes the port: told by each task that comes out
  sys_uart_cfg_t cfg;   // what CREATE made it with
  line_rx_t uart_rx;
#if USB_JTAG
  line_rx_t usb_rx;
#endif
  uint8_t line[1u + COBS_MAX + 1u];  // TX task only: a 0x00 before it, so a text line before ends there; one after
} port_t;

_Static_assert(offsetof(port_t, provider) == 0, "the provider's send casts its pointer back to the port");

// The answer sys_uart_transact() waits for: its fields under s_transact_lock.
static struct {
  uint8_t* rx;
  uint32_t cap;
  uint32_t len;
  bool waiting;
} s_transact;
static portMUX_TYPE s_transact_lock = portMUX_INITIALIZER_UNLOCKED;

static port_t s_console;
static struct {
  port_t* port[UART_NUM_MAX];  // the link and the ports CREATE made, by UART; NULL: none. Under sys_uart_lock
} s_uart;

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

/* Bytes to the port's outputs. The other ports' TX tasks wait for room in the FIFO: nothing else waits on it. The
 * console's UART at its baud rate is far slower than USB: a frame that does not fit its TX buffer now is not written
 * there (its host misses it, as a dropped frame), so a USB host is never held to the UART's pace. */
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

// A task of a port that is being removed: told, it says it is out, to whoever removes the port, and ends.
static void task_out(port_t* p, volatile bool* done) {
  *done = true;
  xTaskNotifyGive(p->closer);
  vTaskDelete(NULL);
}

/* Writes what is queued, then sleeps until the next send notifies it. A send between the empty check and the wait
 * leaves its notification pending, so the wait returns at once. */
static void tx_task(void* arg) {
  port_t* p = arg;
  while (!__atomic_load_n(&p->stop, __ATOMIC_ACQUIRE)) {
    RingbufHandle_t rb = NULL;
    size_t size = 0;
    uint8_t* item = next_item(p, &rb, &size);
    if (item == NULL) {
      (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    if (p->mode == SYS_UART_MODE_FRAMED) {
      write_frame(p, item, (uint32_t)size);
    } else {
      write_out(p, item, (uint32_t)size);
    }
    vRingbufferReturnItem(rb, item);
  }
  task_out(p, &p->tx_done);
}

static err_h provider_send(sys_data_provider_t* self, const uint8_t* data, uint32_t len, uint8_t flags) {
  port_t* p = (port_t*)self;
  if (p->mode == SYS_UART_MODE_DIRECT) {
    // A stream's or SEND's bytes on a port with no tasks: into the driver's TX buffer, never waiting for the line.
    size_t room = 0;
    ERR_CHECK(uart_get_tx_buffer_free_size(p->uart, &room) == ESP_OK && room >= len, ERR_PROV_BUSY, .provider = self->id);
    uart_write_bytes(p->uart, data, len);
    return NULL;
  }
  const bool framed = p->mode == SYS_UART_MODE_FRAMED;
  RingbufHandle_t rb = (flags & SYS_DATA_PROVIDER_SEND_URGENT) ? p->urgent_rb : p->tx_rb;
  const TickType_t wait = (flags & SYS_DATA_PROVIDER_SEND_NOWAIT) ? NO_WAIT : MSEC(CONFIG_SYS_UART_SEND_WAIT_MS);
  uint8_t* item = NULL;
  ERR_CHECK(xRingbufferSendAcquire(rb, (void**)&item, len + (framed ? CRC_BYTES : 0u), wait) == pdTRUE, ERR_PROV_BUSY,
            .provider = self->id);
  // A frame is queued with its CRC, so the TX task encodes the item itself; a raw port's bytes go out as they are.
  memcpy(item, data, len);
  if (framed) {
    const uint16_t crc = crc16(data, len);
    item[len] = (uint8_t)crc;
    item[len + 1] = (uint8_t)(crc >> 8);
  }
  xRingbufferSendComplete(rb, item);
  xTaskNotifyGive(p->tx_task);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * RX: bytes -> the bytes up to each 0x00 -> COBS, CRC16 -> one frame to the provider's user; a raw port's bytes ->
 * messages
 * ------------------------------------------------------------------------- */

/* One candidate: decoded, its CRC checked, delivered whole. Anything else (the ROM's text, a panic, typing, a line
 * hit by noise) fails and is dropped without a word: the host's tool shows those bytes as text. */
static void rx_candidate(port_t* p, uint8_t* buf, uint32_t len) {
  const uint32_t n = cobs_decode(buf, len);  // in its own buffer: the next candidate starts after the user has it
  if (n <= CRC_BYTES) {
    return;
  }
  const uint16_t crc = (uint16_t)(buf[n - 2] | (buf[n - 1] << 8));
  if (crc16(buf, n - CRC_BYTES) != crc) {
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

// A raw message to the user: it ends at a line feed (RAW_LINE), at RAW_RX_MAX bytes, or at a pause (`count` 0, RAW).
static void rx_raw(port_t* p, const uint8_t* data, int count) {
  line_rx_t* rx = &p->uart_rx;
  for (int i = 0; i < count; i++) {
    rx->buf[rx->len++] = data[i];
    if (rx->len == RAW_RX_MAX || (p->mode == SYS_UART_MODE_RAW_LINE && data[i] == '\n')) {
      sys_data_provider_deliver(&p->provider, rx->buf, rx->len);
      rx->len = 0;
    }
  }
  if (count <= 0 && p->mode == SYS_UART_MODE_RAW && rx->len > 0) {
    sys_data_provider_deliver(&p->provider, rx->buf, rx->len);
    rx->len = 0;
  }
}

static void rx_task(void* arg) {
  port_t* p = arg;
  uint8_t chunk[READ_CHUNK];
  while (!__atomic_load_n(&p->stop, __ATOMIC_ACQUIRE)) {
    const int n = uart_read_bytes(p->uart, chunk, sizeof(chunk), MSEC(READ_WAIT_MS));
    if (p->mode == SYS_UART_MODE_FRAMED) {
      rx_bytes(p, &p->uart_rx, chunk, n);
    } else {
      rx_raw(p, chunk, n);
    }
#if USB_JTAG
    if (p->console) {
      rx_bytes(p, &p->usb_rx, chunk, usb_serial_jtag_read_bytes(chunk, sizeof(chunk), MSEC(READ_WAIT_MS)));
    }
#endif
  }
  task_out(p, &p->rx_done);
}

/* ----------------------------------------------------------------------------
 * Ports made at run time (CREATE, the link): made with their driver, rings and tasks, freed with the provider
 * ------------------------------------------------------------------------- */

// The GPIO numbers of a port's pins (UART_PIN_NO_CHANGE: none).
typedef struct {
  int tx;
  int rx;
  int rts;
  int cts;
} pins_t;

/* Takes the port's tasks out: the caller is told by each as it comes out. A task that does not (a TX task held by a
 * line that never frees the FIFO) is deleted. The caller's notification is used, so it is not one that waits on
 * anything else. */
static void stop_tasks(port_t* p) {
  p->closer = xTaskGetCurrentTaskHandle();
  (void)ulTaskNotifyTake(pdTRUE, NO_WAIT);
  __atomic_store_n(&p->stop, true, __ATOMIC_RELEASE);
  uint32_t running = (p->tx_task != NULL) + (p->rx_task != NULL);
  if (p->tx_task != NULL) {
    xTaskNotifyGive(p->tx_task);  // asleep in its wait
  }
  while (running > 0 && ulTaskNotifyTake(pdFALSE, MSEC(STOP_WAIT_MS)) > 0) {
    running--;
  }
  if (p->tx_task != NULL && !p->tx_done) {
    vTaskDelete(p->tx_task);
  }
  if (p->rx_task != NULL && !p->rx_done) {
    vTaskDelete(p->rx_task);
  }
}

// Frees what a port made, in the order it was made; what was not made is skipped.
static void release_port(port_t* p) {
  stop_tasks(p);
  if (p->installed) {
    err_free(ERR_ESP(uart_driver_delete(p->uart)));
  }
  if (p->tx_rb != NULL) {
    vRingbufferDelete(p->tx_rb);
  }
  if (p->urgent_rb != NULL) {
    vRingbufferDelete(p->urgent_rb);
  }
  free(p);
}

// The registry's: the port is out of it and no one has it open.
static void provider_destroy(sys_data_provider_t* self) {
  port_t* p = (port_t*)self;
  xSemaphoreTake(sys_uart_lock, portMAX_DELAY);
  s_uart.port[p->uart] = NULL;
  xSemaphoreGive(sys_uart_lock);
  release_port(p);
}

static err_h install_driver(port_t* p, const sys_uart_cfg_t* cfg, const pins_t* pins) {
  const bool flow = pins->rts != UART_PIN_NO_CHANGE && pins->cts != UART_PIN_NO_CHANGE;
  const uart_config_t uart_cfg = {
      .baud_rate = (int)cfg->baud,
      .data_bits = UART_DATA_8_BITS,
      .parity = cfg->parity == SYS_UART_PARITY_EVEN  ? UART_PARITY_EVEN
                : cfg->parity == SYS_UART_PARITY_ODD ? UART_PARITY_ODD
                                                     : UART_PARITY_DISABLE,
      .stop_bits = cfg->stop_bits == 2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1,
      .flow_ctrl = flow ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 100,
      .source_clk = UART_SCLK_DEFAULT,
  };
  int flags = 0;
#if CONFIG_UART_ISR_IN_IRAM
  flags |= ESP_INTR_FLAG_IRAM;  // a flash write (NVS) stops the cache for milliseconds: the FIFO must still be served
#endif
  /* No TX buffer: the port's TX task writes straight to the FIFO and waits there; its own queue is the only one. A DIRECT
   * port has no queue: the driver's buffer is it, so a write of its owner or a stream's send does not wait on the line. */
  const int tx_buf = p->mode == SYS_UART_MODE_DIRECT ? (int)UART_TX_BUF : 0;
  esp_err_t rc = uart_driver_install(p->uart, CONFIG_SYS_UART_PORT_RX_BUF, tx_buf, 0, NULL, flags);
  if (rc == ESP_OK) {
    p->installed = true;
    rc = uart_param_config(p->uart, &uart_cfg);
  }
  if (rc == ESP_OK) {
    rc = uart_set_pin(p->uart, pins->tx, pins->rx, pins->rts, pins->cts);
  }
  return rc == ESP_OK ? NULL : ERR_WRAP(ERR_ESP(rc), ERR_UART_INIT, .port = p->uart);
}

// The RX and TX tasks of a port with tasks (not DIRECT); false when one could not be made (the caller releases the port).
static bool start_tasks(port_t* p) {
  p->stop = false;
  p->tx_done = false;
  p->rx_done = false;
  return xTaskCreate(tx_task, "uart_tx", CONFIG_SYS_UART_TX_TASK_STACK, p, CONFIG_SYS_UART_TX_TASK_PRIO, &p->tx_task) ==
             pdPASS &&
         xTaskCreate(rx_task, "uart_rx", CONFIG_SYS_UART_TASK_STACK, p, CONFIG_SYS_UART_TASK_PRIO, &p->rx_task) == pdPASS;
}

// Under sys_uart_lock. `extra_state`: SYS_DATA_PROVIDER_STATE_SYSTEM for the board's own (the link).
static err_h open_port_locked(const sys_uart_cfg_t* cfg, const pins_t* pins, uint8_t extra_state) {
  const uint8_t uart = cfg->port;
  ERR_CHECK(s_uart.port[uart] == NULL && !uart_is_driver_installed(uart), ERR_UART_PORT_EXISTS, .port = uart);
  port_t* p = calloc(1, sizeof(*p));
  ERR_CHECK(p != NULL, ERR_UART_NO_MEM, .port = uart);
  p->uart = uart;
  p->mode = cfg->mode;
  p->made = (extra_state & SYS_DATA_PROVIDER_STATE_SYSTEM) == 0;
  p->cfg = *cfg;
  const bool direct = cfg->mode == SYS_UART_MODE_DIRECT;
  if (!direct) {
    p->tx_rb = xRingbufferCreate(CONFIG_SYS_UART_TX_QUEUE, RINGBUF_TYPE_NOSPLIT);
    p->urgent_rb = xRingbufferCreate(URGENT_QUEUE, RINGBUF_TYPE_NOSPLIT);
    if (p->tx_rb == NULL || p->urgent_rb == NULL) {
      release_port(p);
      return ERR_NEW(ERR_UART_NO_MEM, .port = uart);
    }
  }
  err_h err = install_driver(p, cfg, pins);
  if (err != NULL) {
    release_port(p);
    return err;
  }
  p->provider = (sys_data_provider_t){
      .send = provider_send,
      .destroy = provider_destroy,
      .id = cfg->provider,
      .link = SYS_DATA_PROVIDER_LINK_UART,
      .state = extra_state | SYS_DATA_PROVIDER_STATE_WIRED | SYS_DATA_PROVIDER_STATE_UP |
               (cfg->mode == SYS_UART_MODE_FRAMED ? SYS_DATA_PROVIDER_STATE_FRAMED : 0u) |
               (direct ? SYS_DATA_PROVIDER_STATE_NO_RX : 0u),
  };
  if (!direct && !start_tasks(p)) {
    release_port(p);
    return ERR_NEW(ERR_UART_NO_MEM, .port = uart);
  }
  err = sys_data_provider_add(&p->provider);
  if (err != NULL) {
    release_port(p);
    return err;
  }
  s_uart.port[uart] = p;
  return NULL;
}

static err_h open_port(const sys_uart_cfg_t* cfg, const pins_t* pins, uint8_t extra_state) {
  xSemaphoreTake(sys_uart_lock, portMAX_DELAY);
  err_h err = open_port_locked(cfg, pins, extra_state);
  xSemaphoreGive(sys_uart_lock);
  return err;
}

// The chip's UART reaches only the ESP32-S3's own pins (its GPIO matrix): the pin's index is the GPIO.
static err_h check_esp_pin(sys_io_pin_t pin) {
  ERR_CHECK(sys_io_pin_valid(pin), ERR_UART_BAD_PIN, .device = pin.device, .pin = pin.pin);
  sys_device_info_t info;
  ERR_TRY(sys_device_read(pin.device, SYS_DEVICE_OP_INFO, NULL, 0, &info, sizeof(info)));
  ERR_CHECK(info.type_id == SYS_DEVICE_TYPE_ESP32S3_GPIO, ERR_UART_BAD_PIN, .device = pin.device, .pin = pin.pin);
  return NULL;
}

err_h sys_uart_create(const sys_uart_cfg_t* cfg) {
  ERR_CHECK(cfg != NULL, ERR_UART_BAD_ARG);
  ERR_CHECK(cfg->port < UART_NUM_MAX && cfg->port != CONSOLE_PORT, ERR_UART_BAD_PORT, .port = cfg->port);
  ERR_CHECK(cfg->baud >= SYS_UART_BAUD_MIN && cfg->baud <= SYS_UART_BAUD_MAX, ERR_UART_BAD_BAUD, .baud = cfg->baud);
  ERR_CHECK(cfg->mode <= SYS_UART_MODE_DIRECT && cfg->parity <= SYS_UART_PARITY_ODD && cfg->stop_bits >= 1 &&
                cfg->stop_bits <= 2,
            ERR_UART_BAD_FORMAT, .mode = cfg->mode, .parity = cfg->parity, .stop_bits = cfg->stop_bits);
  ERR_TRY(check_esp_pin(cfg->tx));
  ERR_TRY(check_esp_pin(cfg->rx));
  const bool flow = sys_io_pin_valid(cfg->rts) || sys_io_pin_valid(cfg->cts);  // one without the other fails below
  if (flow) {
    ERR_TRY(check_esp_pin(cfg->rts));
    ERR_TRY(check_esp_pin(cfg->cts));
  }
  const pins_t pins = {.tx = cfg->tx.pin,
                       .rx = cfg->rx.pin,
                       .rts = flow ? cfg->rts.pin : UART_PIN_NO_CHANGE,
                       .cts = flow ? cfg->cts.pin : UART_PIN_NO_CHANGE};
  return open_port(cfg, &pins, 0);
}

err_h sys_uart_get(uint8_t port, sys_uart_cfg_t* cfg) {
  ERR_CHECK(cfg != NULL, ERR_UART_BAD_ARG);
  ERR_CHECK(port < UART_NUM_MAX, ERR_UART_BAD_PORT, .port = port);
  xSemaphoreTake(sys_uart_lock, portMAX_DELAY);
  const port_t* p = s_uart.port[port];
  const bool made = p != NULL && p->made;
  if (made) {
    *cfg = p->cfg;
  }
  xSemaphoreGive(sys_uart_lock);
  ERR_CHECK(made, ERR_UART_BAD_PORT, .port = port);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * A DIRECT port: the caller's task writes and reads
 * ------------------------------------------------------------------------- */

// The UART of a DIRECT port. Its owner holds the provider, so it can't be removed meanwhile.
static bool direct_port(uint8_t provider, uart_port_t* uart) {
  bool found = false;
  xSemaphoreTake(sys_uart_lock, portMAX_DELAY);
  for (uint32_t i = 0; i < ARRAY_LEN(s_uart.port) && !found; i++) {
    const port_t* p = s_uart.port[i];
    if (p != NULL && p->provider.id == provider && p->mode == SYS_UART_MODE_DIRECT) {
      *uart = p->uart;
      found = true;
    }
  }
  xSemaphoreGive(sys_uart_lock);
  return found;
}

static err_h direct_write(uart_port_t uart, uint8_t provider, const uint8_t* data, uint32_t len, uint32_t wait_ms) {
  uart_write_bytes(uart, data, len);
  ERR_CHECK(uart_wait_tx_done(uart, MSEC(wait_ms)) == ESP_OK, ERR_UART_TIMEOUT, .provider = provider);
  return NULL;
}

static uint32_t direct_read(uart_port_t uart, uint8_t* data, uint32_t cap, uint32_t wait_ms, uint32_t idle_ms) {
  if (idle_ms == 0) {
    const int n = uart_read_bytes(uart, data, cap, MSEC(wait_ms));
    return n > 0 ? (uint32_t)n : 0;
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
  return total;
}

err_h sys_uart_write(uint8_t provider, const uint8_t* data, uint32_t len, uint32_t wait_ms) {
  ERR_CHECK(data != NULL && len > 0, ERR_UART_BAD_ARG);
  uart_port_t uart;
  ERR_CHECK(direct_port(provider, &uart), ERR_UART_NOT_DIRECT, .provider = provider);
  return direct_write(uart, provider, data, len, wait_ms);
}

err_h sys_uart_read(uint8_t provider, uint8_t* data, uint32_t cap, uint32_t* len, uint32_t wait_ms, uint32_t idle_ms) {
  ERR_CHECK(data != NULL && cap > 0 && len != NULL, ERR_UART_BAD_ARG);
  uart_port_t uart;
  ERR_CHECK(direct_port(provider, &uart), ERR_UART_NOT_DIRECT, .provider = provider);
  *len = direct_read(uart, data, cap, wait_ms, idle_ms);
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
 * The link lent as a DIRECT port (sys_flasher: the ROM bootloader of the board on it)
 * ------------------------------------------------------------------------- */

#if CONFIG_SYS_UART_LINK
// The link's port, made by sys_uart_init() and never removed.
static port_t* link_port(void) {
  xSemaphoreTake(sys_uart_lock, portMAX_DELAY);
  port_t* p = s_uart.port[CONFIG_SYS_UART_LINK_PORT];
  xSemaphoreGive(sys_uart_lock);
  return p != NULL && !p->made ? p : NULL;
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
  port_t* p = link_port();
  ERR_CHECK(p != NULL, ERR_UART_BAD_PORT, .port = CONFIG_SYS_UART_LINK_PORT);
  if (direct == (p->mode == SYS_UART_MODE_DIRECT)) {
    return NULL;
  }
  if (direct) {
    sys_data_provider_set_up(&p->provider, false);  // its session ends; sends are refused while it is lent
    stop_tasks(p);
    p->tx_task = NULL;
    p->rx_task = NULL;
    drop_queued(p);
    p->mode = SYS_UART_MODE_DIRECT;
    return NULL;
  }
  ERR_TRY_ESP(uart_set_baudrate(p->uart, CONFIG_SYS_UART_LINK_BAUD));
  ERR_TRY_ESP(uart_flush_input(p->uart));
  p->uart_rx.len = 0;
  p->uart_rx.overflow = false;
  p->mode = SYS_UART_MODE_FRAMED;
  ERR_CHECK(start_tasks(p), ERR_UART_NO_MEM, .port = p->uart);
  sys_data_provider_set_up(&p->provider, true);  // its session says hello
  return NULL;
}
#else
err_h sys_uart_link_direct(bool direct) {
  (void)direct;
  return ERR_NEW(ERR_UART_BAD_PORT, .port = UART_NUM_MAX);
}
#endif

// A command and its answer on a DIRECT port: holds it for that time, so it refuses a port a driver holds.
static err_h direct_transact(uart_port_t uart, uint8_t provider, const uint8_t* tx, uint32_t tx_len, uint8_t* rx,
                             uint32_t rx_cap, uint32_t* rx_len, uint32_t wait_ms) {
  ERR_TRY(sys_data_provider_open(provider, NULL, NULL));
  err_h err = ERR_ESP(uart_flush_input(uart));
  if (err == NULL && tx_len > 0) {
    err = direct_write(uart, provider, tx, tx_len, CONFIG_SYS_UART_SEND_WAIT_MS);
  }
  if (err == NULL && wait_ms > 0 && rx_cap > 0) {
    *rx_len = direct_read(uart, rx, rx_cap, wait_ms, READ_WAIT_MS);
  }
  err_report(sys_data_provider_close(provider));
  return err;
}

/* ----------------------------------------------------------------------------
 * One command and its answer
 * ------------------------------------------------------------------------- */

// The first message that comes in, on the RX task. A wait that has ended takes nothing: its buffer is its caller's.
static void transact_rx(void* ctx, uint8_t id, const uint8_t* data, uint32_t len) {
  (void)ctx;
  (void)id;
  if (data == NULL) {
    return;
  }
  portENTER_CRITICAL(&s_transact_lock);
  const bool taken = s_transact.waiting;
  if (taken) {
    s_transact.len = len < s_transact.cap ? len : s_transact.cap;
    memcpy(s_transact.rx, data, s_transact.len);
    s_transact.waiting = false;
  }
  portEXIT_CRITICAL(&s_transact_lock);
  if (taken) {
    xSemaphoreGive(sys_uart_transact_sem);
  }
}

err_h sys_uart_transact(uint8_t provider, const uint8_t* tx, uint32_t tx_len, uint8_t* rx, uint32_t rx_cap,
                        uint32_t* rx_len, uint32_t wait_ms) {
  ERR_CHECK(rx_len != NULL && (tx != NULL || tx_len == 0) && (rx != NULL || rx_cap == 0) &&
                wait_ms <= SYS_UART_RAW_WAIT_MAX_MS,
            ERR_UART_BAD_ARG);
  ERR_CHECK(!sys_data_provider_is_framed(provider), ERR_UART_NOT_RAW, .provider = provider);
  *rx_len = 0;
  uart_port_t uart;
  if (direct_port(provider, &uart)) {
    return direct_transact(uart, provider, tx, tx_len, rx, rx_cap, rx_len, wait_ms);
  }
  xSemaphoreTake(sys_uart_transact_mutex, portMAX_DELAY);  // one at a time: one answer's state
  xSemaphoreTake(sys_uart_transact_sem, NO_WAIT);          // a give of a wait that had ended
  const bool wait = wait_ms > 0 && rx_cap > 0;
  portENTER_CRITICAL(&s_transact_lock);
  s_transact.rx = rx;
  s_transact.cap = rx_cap;
  s_transact.len = 0;
  s_transact.waiting = wait;
  portEXIT_CRITICAL(&s_transact_lock);
  // The hook first, so an answer that comes at once is not lost.
  err_h err = sys_data_provider_open(provider, transact_rx, NULL);
  if (err == NULL) {
    if (tx_len > 0) {
      err = sys_data_provider_send(provider, tx, tx_len, 0);
    }
    if (err == NULL && wait) {
      (void)xSemaphoreTake(sys_uart_transact_sem, MSEC(wait_ms));
    }
    err_report(sys_data_provider_close(provider));
  }
  portENTER_CRITICAL(&s_transact_lock);
  s_transact.waiting = false;
  *rx_len = s_transact.len;
  portEXIT_CRITICAL(&s_transact_lock);
  xSemaphoreGive(sys_uart_transact_mutex);
  return err;
}

/* ----------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------- */

err_h sys_uart_init(uint8_t console_provider, uint8_t link_provider) {
  (void)link_provider;  // without the link
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
  // 8N1 at the link's rate on its own pins, with RTS / CTS when both are given.
  const sys_uart_cfg_t link = {.provider = link_provider,
                               .port = CONFIG_SYS_UART_LINK_PORT,
                               .baud = CONFIG_SYS_UART_LINK_BAUD,
                               .mode = SYS_UART_MODE_FRAMED,
                               .parity = SYS_UART_PARITY_NONE,
                               .stop_bits = 1};
  const bool flow = CONFIG_SYS_UART_LINK_RTS_PIN >= 0 && CONFIG_SYS_UART_LINK_CTS_PIN >= 0;
  const pins_t pins = {.tx = CONFIG_SYS_UART_LINK_TX_PIN,
                       .rx = CONFIG_SYS_UART_LINK_RX_PIN,
                       .rts = flow ? CONFIG_SYS_UART_LINK_RTS_PIN : UART_PIN_NO_CHANGE,
                       .cts = flow ? CONFIG_SYS_UART_LINK_CTS_PIN : UART_PIN_NO_CHANGE};
  ERR_TRY(open_port(&link, &pins, SYS_DATA_PROVIDER_STATE_SYSTEM));
#endif
  return NULL;
}
