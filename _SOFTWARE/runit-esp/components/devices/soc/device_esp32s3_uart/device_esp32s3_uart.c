/*
 * The ESP32-S3's UART controller as a device, type 0x004D: implements the UART contract (sys_uart_contract.h) on
 * esp_driver_uart. A port is made by CREATE with any controller, mode, rate and pins (a way to another board, a port
 * for a device, a raw port the app reads); its struct (the registry's, on the heap), its rings and its two tasks live
 * as long as it does. Its reader is one at a time (sys_uart_contract.h); its TX queue is the line's (sys_uart_codec.h).
 */

#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_intr_alloc.h"
#include "sys_io_driver.h"
#include "sys_uart_codec.h"
#include "sys_uart_contract.h"
#include "utils.h"

SYS_DEVICE_TYPE_DECLARE(ESP32S3_UART, 0x004D)

#define RX_TIMEOUT_SYMS  2u    // TOUT after ~2 idle symbols: a RAW message ends at a pause
#define RX_FLOW_THRESH   100u  // RTS asserted at this fill of the 128 B FIFO
#define RX_CHUNK         128u
#define TX_SLICE_WAIT_MS 50u   // a write waits in slices, so a REMOVE never waits on 1200 baud or CTS
#define RX_POLL_MS       100u  // the RX task looks at `stopping` at least this often: a REMOVE waits for it no longer
#define PIN_COUNT        4u    // tx, rx, rts, cts

/** @brief CREATE config: 16 B, packed. */
typedef struct __attribute__((packed)) {
  uint8_t uart_num;              /**< Controller 0..SOC_UART_NUM-1, not one another driver has (the console's) */
  uint8_t mode;                  /**< sys_uart_mode_e */
  uint32_t baud;                 /**< SYS_UART_BAUD_MIN..MAX */
  uint8_t parity;                /**< sys_uart_parity_e */
  uint8_t stop_bits;             /**< 1 or 2 */
  sys_io_pin_t pins[PIN_COUNT];  /**< tx, rx, rts, cts; SYS_IO_PIN_NONE for none (tx or rx at least) */
} esp32s3_uart_cfg_t;

_Static_assert(sizeof(esp32s3_uart_cfg_t) == 16, "esp32s3_uart_cfg_t size");

typedef struct {
  uint8_t id;
  uart_port_t uart;
  uint8_t mode;
  uint8_t parity;
  uint8_t stop_bits;
  uint32_t baud;
  uint32_t init_baud;
  sys_io_claim_t pins[PIN_COUNT];
  sys_device_res_t res;
  SemaphoreHandle_t lock;  // the reader: claimed under it, and an RX_HOOK called under it
  sys_uart_rx_cb_f cb;     // the reader: an RX_HOOK, or
  void* cb_ctx;
  uint8_t owner;           // a subscriber (SYS_DEVICE_ID_NONE: none), its generation and op, or
  uint8_t owner_gen;
  uint16_t owner_op;
  bool reading;            // a RECEIVE / TRANSACT under way
  volatile uint32_t flush_seq;  // FLUSH and TRANSACT count it: the RX task drops what it was assembling
  volatile bool stopping;
  bool installed;
  QueueHandle_t events;     // esp_driver_uart's
  RingbufHandle_t rx_rb;    // messages for RECEIVE while nobody reads
  sys_uart_txq_t txq;
  TaskHandle_t rx_task;
  TaskHandle_t tx_task;
  SemaphoreHandle_t exited;  // given by each task as it ends
  struct __attribute__((packed)) {
    sys_uart_rx_t hdr;  // what a subscriber gets before the message
    uint8_t data[SYS_UART_WIRE_MAX];
  } rx;
  uint8_t chunk[RX_CHUNK];
} port_t;

#define SYS_DEVICE_CTX port_t

static bool has_reader(const port_t* c) {
  return c->cb != NULL || c->owner != SYS_DEVICE_ID_NONE || c->reading;
}

#define IN_USE(c) ERR_NEW(ERR_UART_IN_USE, .port = (c)->id, .owner = (c)->owner)

// Under the lock: what came in so far is dropped, here and at the RX task.
static void flush_rx(port_t* c) {
  c->flush_seq++;
  (void)uart_flush_input(c->uart);
  u_ringbuf_clear(c->rx_rb);
}

/* ----------------------------------------------------------------------------
 * RX task: the driver's events -> one message -> the reader, or the RECEIVE ring
 * ------------------------------------------------------------------------- */

// One whole message in c->rx.data. `seq`: the task's count of flushes; one since: the message is from before it.
// A hook is called under the lock: once RX_HOOK lets go, it is never called again. A subscriber after it.
static void deliver(port_t* c, uint32_t len, uint32_t* seq) {
  if (len == 0) {
    return;
  }
  uint8_t owner;
  uint8_t gen;
  uint16_t op;
  {
    U_MUTEX_GUARD(c->lock);
    const bool flushed = *seq != c->flush_seq;
    *seq = c->flush_seq;
    owner = flushed ? SYS_DEVICE_ID_NONE : c->owner;
    gen = c->owner_gen;
    op = c->owner_op;
    const bool here = !flushed && owner == SYS_DEVICE_ID_NONE;  // not dropped, not a subscriber's
    if (here && c->cb != NULL) {
      c->cb(c->cb_ctx, c->rx.data, len);
    } else if (here) {  // for RECEIVE: when full, the oldest goes
      size_t old_len = 0;
      void* old = NULL;
      while (xRingbufferSend(c->rx_rb, c->rx.data, len, 0) != pdTRUE &&
             (old = xRingbufferReceive(c->rx_rb, &old_len, 0)) != NULL) {
        vRingbufferReturnItem(c->rx_rb, old);
      }
    }
  }
  if (owner != SYS_DEVICE_ID_NONE) {
    c->rx.hdr.port = c->id;
    err_report(sys_device_deliver(owner, gen, op, &c->rx, (uint32_t)sizeof(c->rx.hdr) + len));
  }
}

/* FRAMED: a candidate ends at 0x00 (one longer than any frame is dropped there); RAW_LINE: a line at '\n' (a '\r'
 * before it goes); RAW and RAW_LINE: CMD_FRAME_MAX bytes are a message too. */
static void feed(port_t* c, const uint8_t* buf, int n, uint32_t* len, bool* skip, uint32_t* seq) {
  const bool framed = c->mode == SYS_UART_MODE_FRAMED;
  for (int i = 0; i < n; i++) {
    const uint8_t b = buf[i];
    if (framed && b == 0) {
      deliver(c, *skip ? 0 : sys_uart_unframe(c->rx.data, *len), seq);
      *len = 0;
      *skip = false;
    } else if (c->mode == SYS_UART_MODE_RAW_LINE && b == '\n') {
      deliver(c, (*len > 0 && c->rx.data[*len - 1u] == '\r') ? *len - 1u : *len, seq);
      *len = 0;
    } else if (*len < (framed ? sizeof(c->rx.data) : CMD_FRAME_MAX)) {
      c->rx.data[(*len)++] = b;
      if (!framed && *len == CMD_FRAME_MAX) {
        deliver(c, *len, seq);
        *len = 0;
      }
    } else {
      *skip = true;
    }
  }
}

static void rx_task(void* arg) {
  port_t* c = arg;
  uint32_t len = 0;  // the message being assembled
  bool skip = false;
  uint32_t seq = c->flush_seq;
  uart_event_t ev;
  while (!c->stopping) {
    if (xQueueReceive(c->events, &ev, MSEC(RX_POLL_MS)) != pdTRUE) {
      continue;
    }
    if (seq != c->flush_seq) {
      seq = c->flush_seq;
      len = 0;
      skip = false;
    }
    if (ev.type == UART_DATA) {
      int got = 0;
      while ((got = uart_read_bytes(c->uart, c->chunk, sizeof(c->chunk), 0)) > 0) {
        feed(c, c->chunk, got, &len, &skip, &seq);
      }
      if (c->mode == SYS_UART_MODE_RAW && ev.timeout_flag) {  // a pause in the line: a RAW message ends
        deliver(c, len, &seq);
        len = 0;
      }
    } else if (ev.type == UART_FIFO_OVF || ev.type == UART_BUFFER_FULL) {
      (void)uart_flush_input(c->uart);
      (void)xQueueReset(c->events);
      len = 0;
      skip = c->mode == SYS_UART_MODE_FRAMED;  // to the next 0x00
      err_report(SYS_EVENT_PUBLISH_DEFER(UART_EV_OVERFLOW, c->id, 0, .dropped = 1));
    }
  }
  xSemaphoreGive(c->exited);
  vTaskDelete(NULL);
}

/* ----------------------------------------------------------------------------
 * TX task: the queue's next frame onto the line
 * ------------------------------------------------------------------------- */

static void tx_task(void* arg) {
  port_t* c = arg;
  while (!c->stopping) {
    const uint32_t len = sys_uart_txq_pop(&c->txq, portMAX_DELAY);
    uint32_t pos = 0;
    while (pos < len && !c->stopping) {
      const int n = uart_tx_chars(c->uart, (const char*)(c->txq.line + pos), len - pos);
      if (n > 0) {
        pos += (uint32_t)n;
      } else {
        (void)uart_wait_tx_done(c->uart, MSEC(TX_SLICE_WAIT_MS));
      }
    }
  }
  xSemaphoreGive(c->exited);
  vTaskDelete(NULL);
}

static err_h send(port_t* c, cmd_in_t frame, uint16_t wait_ms, uint8_t flags) {
  ERR_CHECK(frame.len > 0 && frame.len <= CMD_FRAME_MAX && wait_ms <= SYS_UART_WAIT_MAX_MS, ERR_BAD_ARG);
  const TickType_t wait = wait_ms == 0 ? 0 : MSEC(wait_ms);
  ERR_CHECK(sys_uart_txq_push(&c->txq, frame, flags, wait), ERR_UART_BUSY, .port = c->id);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Contract operations (CONTRACT_UART)
 * ------------------------------------------------------------------------- */

OP_DECLARE_READ(CONTRACT_UART, OP_INFO) {
  const sys_uart_info_t info = {.mode = c->mode, .baud = c->baud, .parity = c->parity, .stop_bits = c->stop_bits};
  return cmd_reply(out, &info, sizeof(info));
}

OP_DECLARE_READ(CONTRACT_UART, OP_BAUD) {
  return cmd_reply(out, &c->baud, sizeof(c->baud));
}

OP_DECLARE_WRITE(CONTRACT_UART, OP_BAUD) {
  ERR_CHECK(*args >= SYS_UART_BAUD_MIN && *args <= SYS_UART_BAUD_MAX, ERR_UART_BAD_BAUD, .baud = *args);
  (void)uart_wait_tx_done(c->uart, MSEC(100));
  ERR_TRY(ERR_ESP(uart_set_baudrate(c->uart, *args)));
  c->baud = *args;
  return NULL;
}

// Refused on a port a subscriber holds (but for it), and on a FRAMED one a hook holds: raw bytes would break frames.
OP_DECLARE_WRITE_TAIL(CONTRACT_UART, OP_SEND) {
  const bool held = c->owner != SYS_DEVICE_ID_NONE ? c->owner != sys_device_caller()
                                                   : c->cb != NULL && c->mode == SYS_UART_MODE_FRAMED;
  ERR_CHECK(!held, ERR_UART_IN_USE, .port = c->id, .owner = c->owner);
  return send(c, (cmd_in_t){tail, tail_len}, args->wait_ms, args->flags);
}

OP_DECLARE_WRITE(CONTRACT_UART, OP_SEND_FRAME) {
  return send(c, args->frame, args->wait_ms, args->flags);
}

// RECEIVE (`tx` empty) and TRANSACT: the caller the port's reader for the call; TRANSACT flushes, sends, then reads.
static err_h read_one(port_t* c, cmd_in_t tx, uint16_t wait_ms, cmd_out_t* out) {
  ERR_CHECK(wait_ms <= SYS_UART_WAIT_MAX_MS, ERR_BAD_ARG);
  bool busy;
  {
    U_MUTEX_GUARD(c->lock);
    busy = has_reader(c);
    if (!busy) {
      c->reading = true;
      if (tx.len > 0) {
        flush_rx(c);
      }
    }
  }
  ERR_CHECK(!busy, ERR_UART_IN_USE, .port = c->id, .owner = c->owner);
  const TickType_t start = xTaskGetTickCount();
  const TickType_t total = MSEC(wait_ms);
  err_h err = tx.len > 0 ? send(c, tx, wait_ms, 0) : NULL;
  if (err == NULL) {
    const TickType_t spent = xTaskGetTickCount() - start;
    size_t len = 0;
    void* item = xRingbufferReceive(c->rx_rb, &len, spent < total ? total - spent : 0);
    if (item != NULL) {  // none came: an empty answer
      err = cmd_reply(out, item, (uint32_t)len);
      vRingbufferReturnItem(c->rx_rb, item);
    }
  }
  c->reading = false;
  return err;
}

OP_DECLARE_READ(CONTRACT_UART, OP_RECEIVE) {
  return read_one(c, (cmd_in_t){0}, args->wait_ms, out);
}

OP_DECLARE_WRITE_TAIL(CONTRACT_UART, OP_TRANSACT) {
  ERR_CHECK(tail_len > 0, ERR_BAD_ARG);
  return read_one(c, (cmd_in_t){tail, tail_len}, args->wait_ms, out);
}

OP_DECLARE_WRITE(CONTRACT_UART, OP_FLUSH) {
  bool busy;
  {
    U_MUTEX_GUARD(c->lock);
    busy = has_reader(c);
    if (!busy) {
      flush_rx(c);
    }
  }
  ERR_CHECK(!busy, ERR_UART_IN_USE, .port = c->id, .owner = c->owner);
  return NULL;
}

// The caller (a device, in its create()) the reader: each message to its own op. `op` 0: it lets go.
OP_DECLARE_WRITE(CONTRACT_UART, OP_SUBSCRIBE) {
  const uint8_t caller = sys_device_caller();
  ERR_CHECK(caller != SYS_DEVICE_ID_NONE, ERR_BAD_ARG);
  bool busy;
  {
    U_MUTEX_GUARD(c->lock);
    busy = args->op != 0 && has_reader(c);
    if (args->op == 0 && c->owner == caller) {
      c->owner = SYS_DEVICE_ID_NONE;
    } else if (args->op != 0 && !busy) {
      c->owner = caller;
      c->owner_gen = sys_device_gen(caller);
      c->owner_op = args->op;
      flush_rx(c);
    }
  }
  ERR_CHECK(!busy, ERR_UART_IN_USE, .port = c->id, .owner = c->owner);
  return NULL;
}

// A C reader (a provider adapter). `cb` NULL: the hook with this `ctx` lets go.
OP_DECLARE_WRITE(CONTRACT_UART, OP_RX_HOOK) {
  bool busy;
  {
    U_MUTEX_GUARD(c->lock);
    busy = args->cb != NULL && has_reader(c);
    if (args->cb == NULL && c->cb_ctx == args->ctx) {
      c->cb = NULL;
    } else if (args->cb != NULL && !busy) {
      c->cb = args->cb;
      c->cb_ctx = args->ctx;
      flush_rx(c);
    }
  }
  ERR_CHECK(!busy, ERR_UART_IN_USE, .port = c->id, .owner = c->owner);
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  (void)uart_wait_tx_done(c->uart, MSEC(100));
  ERR_TRY(ERR_ESP(uart_set_baudrate(c->uart, c->init_baud)));
  c->baud = c->init_baud;
  sys_uart_txq_clear(&c->txq);
  U_MUTEX_GUARD(c->lock);
  flush_rx(c);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Create and release
 * ------------------------------------------------------------------------- */

// A port has no outputs to make safe: suspended (the safe state), it keeps carrying a session's frames.
static err_h suspend(port_t* c) {
  return NULL;
}

static err_h resume(port_t* c) {
  return NULL;
}

// Its resource, also after a create() that failed half-way: the tasks end first (the RX task within RX_POLL_MS),
// then the hook hears the port go.
static err_h port_release(void* arg) {
  port_t* c = arg;
  c->stopping = true;
  const uint32_t tasks = (c->rx_task != NULL) + (c->tx_task != NULL);
  if (c->installed) {
    (void)uart_set_hw_flow_ctrl(c->uart, UART_HW_FLOWCTRL_DISABLE, 0);
    (void)uart_disable_rx_intr(c->uart);
  }
  if (c->tx_task != NULL) {
    xTaskNotifyGive(c->tx_task);
  }
  for (uint32_t i = 0; i < tasks; i++) {
    (void)xSemaphoreTake(c->exited, portMAX_DELAY);
  }
  if (c->cb != NULL) {
    c->cb(c->cb_ctx, NULL, 0);
  }
  if (c->installed) {
    (void)uart_driver_delete(c->uart);
  }
  sys_uart_txq_free(&c->txq);
  if (c->rx_rb != NULL) {
    vRingbufferDelete(c->rx_rb);
  }
  if (c->exited != NULL) {
    vSemaphoreDelete(c->exited);
  }
  if (c->lock != NULL) {
    vSemaphoreDelete(c->lock);
  }
  return NULL;
}

static err_h validate_cfg(uint8_t id, const esp32s3_uart_cfg_t* cfg) {
  ERR_CHECK_RANGE(cfg->uart_num, 0, SOC_UART_NUM - 1);
  ERR_CHECK(cfg->mode <= SYS_UART_MODE_RAW_LINE && cfg->parity <= SYS_UART_PARITY_ODD &&
                (cfg->stop_bits == 1u || cfg->stop_bits == 2u) &&
                (sys_io_pin_valid(cfg->pins[0]) || sys_io_pin_valid(cfg->pins[1])),
            ERR_BAD_ARG);
  ERR_CHECK(cfg->baud >= SYS_UART_BAUD_MIN && cfg->baud <= SYS_UART_BAUD_MAX, ERR_UART_BAD_BAUD, .baud = cfg->baud);
  for (uint32_t i = 0; i < PIN_COUNT; i++) {
    const bool output = i == 0 || i == 2;  // tx, rts
    const sys_io_pin_t p = cfg->pins[i];
    ERR_CHECK(!sys_io_pin_valid(p) || (output ? GPIO_IS_VALID_OUTPUT_GPIO(p.pin) : GPIO_IS_VALID_GPIO(p.pin)),
              ERR_DEV_BAD_PIN, .id = id, .pin = p.pin);
  }
  ERR_CHECK(!uart_is_driver_installed(cfg->uart_num), ERR_UART_IN_USE, .port = id, .owner = SYS_DEVICE_ID_NONE);
  return NULL;
}

static int gpio_of(sys_io_pin_t p) {
  return sys_io_pin_valid(p) ? (int)p.pin : UART_PIN_NO_CHANGE;
}

static err_h create(uint8_t id, port_t* c, const esp32s3_uart_cfg_t* cfg) {
  ERR_TRY(validate_cfg(id, cfg));
  for (uint32_t i = 0; i < PIN_COUNT; i++) {
    ERR_TRY(sys_io_claim_peripheral(id, &c->pins[i], cfg->pins[i]));  // the registry's resources: c stays where it is
  }
  c->id = id;
  c->uart = (uart_port_t)cfg->uart_num;
  c->mode = cfg->mode;
  c->parity = cfg->parity;
  c->stop_bits = cfg->stop_bits;
  c->baud = cfg->baud;
  c->init_baud = cfg->baud;
  c->owner = SYS_DEVICE_ID_NONE;
  ERR_TRY(sys_device_res_add(id, &c->res, port_release, c));  // from here on, a failure releases what was made
  c->lock = xSemaphoreCreateMutex();
  c->exited = xSemaphoreCreateCounting(2, 0);
  c->rx_rb = xRingbufferCreate(CONFIG_ESP32S3_UART_RX_QUEUE, RINGBUF_TYPE_NOSPLIT);
  const bool made = sys_uart_txq_init(&c->txq, CONFIG_ESP32S3_UART_TX_QUEUE, cfg->mode == SYS_UART_MODE_FRAMED);
  ERR_CHECK(made && c->lock != NULL && c->exited != NULL && c->rx_rb != NULL, ERR_NO_MEM,
            .size = CONFIG_ESP32S3_UART_TX_QUEUE + CONFIG_ESP32S3_UART_RX_QUEUE);

#if CONFIG_UART_ISR_IN_IRAM
  const int intr = ESP_INTR_FLAG_IRAM;
#else
  const int intr = 0;
#endif
  esp_err_t rc = uart_driver_install(c->uart, CONFIG_ESP32S3_UART_ISR_RX_BUF, 0, CONFIG_ESP32S3_UART_EVT_QUEUE_LEN,
                                     &c->events, intr);
  c->installed = rc == ESP_OK;
  const uart_config_t ucfg = {
      .baud_rate = (int)cfg->baud,
      .data_bits = UART_DATA_8_BITS,
      .parity = cfg->parity == SYS_UART_PARITY_EVEN  ? UART_PARITY_EVEN
                : cfg->parity == SYS_UART_PARITY_ODD ? UART_PARITY_ODD
                                                     : UART_PARITY_DISABLE,
      .stop_bits = cfg->stop_bits == 2u ? UART_STOP_BITS_2 : UART_STOP_BITS_1,
      .flow_ctrl = (uart_hw_flowcontrol_t)((sys_io_pin_valid(cfg->pins[2]) ? UART_HW_FLOWCTRL_RTS : 0) |
                                           (sys_io_pin_valid(cfg->pins[3]) ? UART_HW_FLOWCTRL_CTS : 0)),
      .rx_flow_ctrl_thresh = RX_FLOW_THRESH,
      .source_clk = UART_SCLK_DEFAULT,
  };
  rc = rc == ESP_OK ? uart_param_config(c->uart, &ucfg) : rc;
  rc = rc == ESP_OK ? uart_set_pin(c->uart, gpio_of(cfg->pins[0]), gpio_of(cfg->pins[1]), gpio_of(cfg->pins[2]),
                                   gpio_of(cfg->pins[3]))
                    : rc;
  rc = rc == ESP_OK ? uart_set_rx_timeout(c->uart, RX_TIMEOUT_SYMS) : rc;
  ERR_CHECK(rc == ESP_OK, ERR_UART_INIT, .port = id);
  (void)uart_set_always_rx_timeout(c->uart, true);

  (void)xTaskCreate(rx_task, "uart_rx", CONFIG_ESP32S3_UART_RX_TASK_STACK, c, CONFIG_ESP32S3_UART_RX_TASK_PRIO,
                    &c->rx_task);
  (void)xTaskCreate(tx_task, "uart_tx", CONFIG_ESP32S3_UART_TX_TASK_STACK, c, CONFIG_ESP32S3_UART_TX_TASK_PRIO,
                    &c->tx_task);
  c->txq.task = c->tx_task;
  ERR_CHECK(c->rx_task != NULL && c->tx_task != NULL, ERR_NO_MEM,
            .size = CONFIG_ESP32S3_UART_RX_TASK_STACK + CONFIG_ESP32S3_UART_TX_TASK_STACK);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Type registration
 * ------------------------------------------------------------------------- */

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),  OP_ENTRY_R(CONTRACT_UART, OP_INFO),
    OP_ENTRY(CONTRACT_UART, OP_BAUD),           OP_ENTRY_W(CONTRACT_UART, OP_SEND),
    OP_ENTRY_R(CONTRACT_UART, OP_RECEIVE),      OP_ENTRY_W(CONTRACT_UART, OP_FLUSH),
    OP_ENTRY_W(CONTRACT_UART, OP_SUBSCRIBE),    OP_ENTRY_W(CONTRACT_UART, OP_TRANSACT),
    OP_ENTRY_W(CONTRACT_UART, OP_SEND_FRAME),   OP_ENTRY_W(CONTRACT_UART, OP_RX_HOOK),
};

SYS_DEVICE_TYPE_DEFINE(ESP32S3_UART, esp32s3_uart_cfg_t, create, suspend, resume, k_ops)
