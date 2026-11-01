#include "sys_uart.h"

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

#define UART_PORT       CONFIG_ESP_CONSOLE_UART_NUM
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

U_TASK_DEFINE(sys_uart_rx_task_h, CONFIG_SYS_UART_TASK_STACK);
U_TASK_DEFINE(sys_uart_tx_task_h, CONFIG_SYS_UART_TX_TASK_STACK);
U_RINGBUFFER_DEFINE(sys_uart_tx_rb, CONFIG_SYS_UART_TX_QUEUE, RINGBUF_TYPE_NOSPLIT);
U_RINGBUFFER_DEFINE(sys_uart_urgent_rb, URGENT_QUEUE, RINGBUF_TYPE_NOSPLIT);

static sys_data_provider_t s_provider;  // the console's port: framed, wired, always up

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

/* One encoded frame to both console outputs. The UART at the console's baud rate is far slower than USB: a frame that
 * does not fit its TX buffer now is not written there (its host misses it, as a dropped frame), so a USB host is never
 * held to the UART's pace. */
static void write_out(const uint8_t* data, uint32_t len) {
  size_t room = 0;
  if (uart_get_tx_buffer_free_size(UART_PORT, &room) == ESP_OK && room >= len) {
    uart_write_bytes(UART_PORT, data, len);
  }
#if USB_JTAG
  // The driver takes no write longer than its buffer: a frame goes in pieces of half of it, until the host stops reading.
  for (uint32_t done = 0; done < len && usb_serial_jtag_is_connected();) {
    const uint32_t piece = len - done < USB_JTAG_TX_BUF / 2u ? len - done : USB_JTAG_TX_BUF / 2u;
    const int n = usb_serial_jtag_write_bytes(data + done, piece, MSEC(CONFIG_SYS_UART_SEND_WAIT_MS));
    if (n <= 0) {
      break;
    }
    done += (uint32_t)n;
  }
#endif
}

static void write_frame(const uint8_t* frame, uint32_t len) {
  static uint8_t raw[RAW_MAX];
  static uint8_t line[1u + COBS_MAX + 1u];  // a 0x00 before it, so a text line before ends there; one after
  memcpy(raw, frame, len);
  const uint16_t crc = crc16(frame, len);
  raw[len] = (uint8_t)crc;
  raw[len + 1] = (uint8_t)(crc >> 8);
  line[0] = 0;
  const uint32_t n = cobs_encode(raw, len + CRC_BYTES, line + 1);
  line[1 + n] = 0;
  write_out(line, n + 2u);
}

static void tx_task(void* arg) {
  (void)arg;
  while (1) {
    size_t size = 0;
    uint8_t* item = xRingbufferReceive(sys_uart_urgent_rb, &size, NO_WAIT);
    RingbufHandle_t rb = sys_uart_urgent_rb;
    if (item == NULL) {
      item = xRingbufferReceive(sys_uart_tx_rb, &size, NO_WAIT);
      rb = sys_uart_tx_rb;
    }
    if (item != NULL) {
      write_frame(item, (uint32_t)size);
      vRingbufferReturnItem(rb, item);
      continue;
    }
    uint32_t bits = 0;
    U_NOTIFY_AWAIT(WAIT_FOREVER, &bits);
  }
}

static err_h provider_send(sys_data_provider_t* self, const uint8_t* data, uint32_t len, uint8_t flags) {
  RingbufHandle_t rb = (flags & SYS_DATA_PROVIDER_SEND_URGENT) ? sys_uart_urgent_rb : sys_uart_tx_rb;
  const TickType_t wait = (flags & SYS_DATA_PROVIDER_SEND_NOWAIT) ? NO_WAIT : MSEC(CONFIG_SYS_UART_SEND_WAIT_MS);
  ERR_CHECK(xRingbufferSend(rb, data, len, wait) == pdTRUE, ERR_PROV_BUSY, .link = self->id.link,
            .channel = self->id.channel);
  xTaskNotify(sys_uart_tx_task_h, NOTIFY_FRAME, eSetBits);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * RX: console bytes -> the bytes up to each 0x00 -> COBS, CRC16 -> one frame to the provider's user
 * ------------------------------------------------------------------------- */

// One candidate per input (UART, USB-Serial-JTAG): the bytes since the last 0x00.
typedef struct {
  uint8_t buf[COBS_MAX];
  uint32_t len;
  bool overflow;  // longer than any frame: dropped at its 0x00
} line_rx_t;

static line_rx_t s_uart_rx;
#if USB_JTAG
static line_rx_t s_usb_rx;
#endif
static uint8_t s_frame[RAW_MAX];  // RX task only

/* One candidate: decoded, its CRC checked, delivered whole. Anything else (the ROM's text, a panic, typing) fails
 * and is dropped without a word: the host's tool shows those bytes as text. */
static void rx_candidate(const uint8_t* data, uint32_t len) {
  const uint32_t n = cobs_decode(data, len, s_frame, sizeof(s_frame));
  if (n <= CRC_BYTES) {
    return;
  }
  const uint16_t crc = (uint16_t)(s_frame[n - 2] | (s_frame[n - 1] << 8));
  if (crc16(s_frame, n - CRC_BYTES) != crc) {
    return;
  }
  sys_data_provider_deliver(&s_provider, s_frame, n - CRC_BYTES);
}

static void rx_bytes(line_rx_t* rx, const uint8_t* data, int count) {
  for (int i = 0; i < count; i++) {
    if (data[i] == 0) {
      if (rx->len > 0 && !rx->overflow) {
        rx_candidate(rx->buf, rx->len);
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
    ERR_TRY_ESP(uart_driver_install(UART_PORT, CONFIG_SYS_UART_DRIVER_RX_BUF, UART_TX_BUF, 0, NULL, 0));
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
  if (sys_uart_tx_task_h == NULL) {
    U_TASK_START(sys_uart_tx_task_h, tx_task, NULL, CONFIG_SYS_UART_TX_TASK_PRIO);
  }
  s_provider = (sys_data_provider_t){
      .send = provider_send,
      .max_frame = CMD_FRAME_MAX,
      .id = SYS_UART_PROVIDER,
      .state = SYS_DATA_PROVIDER_STATE_FRAMED | SYS_DATA_PROVIDER_STATE_WIRED,
  };
  ERR_TRY(sys_data_provider_add(&s_provider));
  if (sys_uart_rx_task_h == NULL) {
    U_TASK_START(sys_uart_rx_task_h, rx_task, NULL, CONFIG_SYS_UART_TASK_PRIO);
  }
  return NULL;
}
