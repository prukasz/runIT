/*
 * Static slot pool, ESP-IDF UART setup/teardown, and TX/RX FreeRTOS tasks for device_esp32s3_uart (esp32s3_uart.h).
 */

#include "esp32s3_uart.h"
#include "esp_intr_alloc.h"

/* ----------------------------------------------------------------------------
 * Static configuration checks & slot pool
 * ------------------------------------------------------------------------- */

#define RX_TIMEOUT_SYMS  2u     // TOUT after ~2 symbol times idle (guarantees burst end in RAW mode)
#define RX_FLOW_THRESH   100u   // RTS assertion threshold (128 B HW FIFO)
#define PARK_WAIT_MS     2000u
#define TX_SLICE_WAIT_MS 50u

_Static_assert(CONFIG_ESP32S3_UART_MAX_PORTS >= 1 && CONFIG_ESP32S3_UART_MAX_PORTS <= SOC_UART_NUM,
               "ESP32S3_UART_MAX_PORTS out of SOC_UART_NUM range");
_Static_assert(CONFIG_ESP32S3_UART_ISR_RX_BUF >= 256u, "ESP-IDF UART RX buffer must exceed 128 B FIFO");
_Static_assert(CONFIG_ESP32S3_UART_TX_QUEUE % 4 == 0 &&
                   CONFIG_ESP32S3_UART_TX_QUEUE >= 2u * ESP32S3_UART_ITEM_BYTES(ESP32S3_UART_RAW_MAX),
               "ESP32S3_UART_TX_QUEUE must be 4-aligned and hold at least two full frames");
_Static_assert(CONFIG_ESP32S3_UART_RX_QUEUE % 4 == 0 &&
                   CONFIG_ESP32S3_UART_RX_QUEUE >= ESP32S3_UART_ITEM_BYTES(CMD_FRAME_MAX),
               "ESP32S3_UART_RX_QUEUE must be 4-aligned and hold one max message");

static esp32s3_uart_slot_t s_slots[CONFIG_ESP32S3_UART_MAX_PORTS];
static StaticSemaphore_t s_slots_lock_buf;
static SemaphoreHandle_t s_slots_lock;

__attribute__((constructor)) static void init_slots_lock(void) {
  s_slots_lock = xSemaphoreCreateMutexStatic(&s_slots_lock_buf);
}

/* ----------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------- */

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

void esp32s3_uart_drain_ring(RingbufHandle_t rb) {
  size_t sz = 0;
  for (;;) {
    void* item = xRingbufferReceive(rb, &sz, 0);
    if (item == NULL) {
      break;
    }
    vRingbufferReturnItem(rb, item);
  }
}

void esp32s3_uart_flush_rx_locked(esp32s3_uart_slot_t* slot) {
  __atomic_add_fetch(&slot->rx_flush_seq, 1u, __ATOMIC_RELEASE);
  (void)uart_flush_input(slot->uart);
  esp32s3_uart_drain_ring(slot->rx_rb);
}

bool esp32s3_uart_has_reader_locked(const esp32s3_uart_slot_t* slot, uint8_t* out_owner) {
  if (slot->sub_id != SYS_DEVICE_ID_NONE) {
    if (out_owner != NULL) {
      *out_owner = slot->sub_id;
    }
    return true;
  }
  if (slot->hook.cb != NULL || slot->rx_reader_busy) {
    if (out_owner != NULL) {
      *out_owner = SYS_DEVICE_ID_NONE;
    }
    return true;
  }
  return false;
}

void esp32s3_uart_notify_rx_wakeup(esp32s3_uart_slot_t* slot) {
  if (slot->evt_queue != NULL) {
    const uart_event_t ev = {.type = UART_EVENT_MAX};
    (void)xQueueSendToFront(slot->evt_queue, &ev, 0);
  }
}

static uart_parity_t idf_parity(uint8_t parity) {
  switch (parity) {
    case SYS_UART_PARITY_EVEN:
      return UART_PARITY_EVEN;
    case SYS_UART_PARITY_ODD:
      return UART_PARITY_ODD;
    default:
      return UART_PARITY_DISABLE;
  }
}

static uart_stop_bits_t idf_stop_bits(uint8_t stop_bits) {
  return stop_bits == 2u ? UART_STOP_BITS_2 : UART_STOP_BITS_1;
}

static uart_hw_flowcontrol_t idf_flow_ctrl(sys_io_pin_t rts, sys_io_pin_t cts) {
  const bool has_rts = sys_io_pin_valid(rts);
  const bool has_cts = sys_io_pin_valid(cts);
  if (has_rts && has_cts) {
    return UART_HW_FLOWCTRL_CTS_RTS;
  }
  if (has_rts) {
    return UART_HW_FLOWCTRL_RTS;
  }
  if (has_cts) {
    return UART_HW_FLOWCTRL_CTS;
  }
  return UART_HW_FLOWCTRL_DISABLE;
}

/* ----------------------------------------------------------------------------
 * TX task: drains urgent_rb before each normal frame, slice-written so REMOVE never stalls on 1200 baud / CTS
 * ------------------------------------------------------------------------- */

static uint8_t* next_tx_item(esp32s3_uart_slot_t* slot, RingbufHandle_t* out_rb, size_t* out_len) {
  *out_rb = slot->urgent_rb;
  uint8_t* item = xRingbufferReceive(*out_rb, out_len, 0);
  if (item == NULL) {
    *out_rb = slot->tx_rb;
    item = xRingbufferReceive(*out_rb, out_len, 0);
  }
  return item;
}

static void write_interruptible(esp32s3_uart_slot_t* slot, const uint8_t* data, size_t len) {
  size_t pos = 0;
  while (pos < len && !__atomic_load_n(&slot->stopping, __ATOMIC_ACQUIRE)) {
    const int n = uart_tx_chars(slot->uart, (const char*)(data + pos), (uint32_t)(len - pos));
    if (n > 0) {
      pos += (size_t)n;
    } else {
      (void)uart_wait_tx_done(slot->uart, MSEC(TX_SLICE_WAIT_MS));
    }
  }
}

static void tx_task_fn(void* arg) {
  esp32s3_uart_slot_t* slot = arg;
  for (;;) {
    if (__atomic_load_n(&slot->stopping, __ATOMIC_ACQUIRE)) {
      xSemaphoreGive(slot->parked_sem);
      while (__atomic_load_n(&slot->stopping, __ATOMIC_ACQUIRE)) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      }
    }
    while (!__atomic_load_n(&slot->stopping, __ATOMIC_ACQUIRE)) {
      RingbufHandle_t rb = NULL;
      size_t len = 0;
      uint8_t* item = next_tx_item(slot, &rb, &len);
      if (item == NULL) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        continue;
      }
      size_t tx_len = 0;
      if (slot->mode == SYS_UART_MODE_FRAMED) {
        slot->line_tx[0] = 0;
        const uint32_t n = sys_uart_cobs_encode(item, (uint32_t)len, slot->line_tx + 1);
        slot->line_tx[1u + n] = 0;
        tx_len = (size_t)n + 2u;
      } else {
        tx_len = len <= sizeof(slot->line_tx) ? len : sizeof(slot->line_tx);
        memcpy(slot->line_tx, item, tx_len);
      }
      vRingbufferReturnItem(rb, item);
      write_interruptible(slot, slot->line_tx, tx_len);
    }
  }
}

/* ----------------------------------------------------------------------------
 * RX task: hardware event-driven (UART_DATA + timeout_flag / overflow)
 * ------------------------------------------------------------------------- */

static void publish_overflow_if_due(esp32s3_uart_slot_t* slot, bool force) {
  uint16_t dropped = 0;
  uint8_t src = SYS_DEVICE_ID_NONE;
  xSemaphoreTake(slot->lock, portMAX_DELAY);
  if (slot->ovf_pending > 0) {
    const TickType_t now = xTaskGetTickCount();
    const TickType_t min_gap = MSEC(CONFIG_ESP32S3_UART_OVERFLOW_THROTTLE_MS);
    if (force || !slot->ovf_sent_once || (now - slot->last_ovf_tick) >= min_gap) {
      dropped = slot->ovf_pending;
      slot->ovf_pending = 0;
      slot->ovf_sent_once = true;
      slot->last_ovf_tick = now;
      src = slot->dev_id;
    }
  }
  xSemaphoreGive(slot->lock);
  if (dropped > 0) {
    err_report(SYS_EVENT_PUBLISH_DEFER(UART_EV_OVERFLOW, src, 0, .dropped = dropped));
  }
}

static TickType_t rx_event_wait_ticks(esp32s3_uart_slot_t* slot) {
  TickType_t wait = portMAX_DELAY;
  xSemaphoreTake(slot->lock, portMAX_DELAY);
  if (slot->ovf_pending > 0) {
    const TickType_t now = xTaskGetTickCount();
    const TickType_t min_gap = MSEC(CONFIG_ESP32S3_UART_OVERFLOW_THROTTLE_MS);
    const TickType_t elapsed = now - slot->last_ovf_tick;
    wait = (!slot->ovf_sent_once || elapsed >= min_gap) ? 0 : (min_gap - elapsed);
  }
  xSemaphoreGive(slot->lock);
  return wait;
}

static void record_overflow(esp32s3_uart_slot_t* slot) {
  xSemaphoreTake(slot->lock, portMAX_DELAY);
  if (slot->ovf_pending < UINT16_MAX) {
    slot->ovf_pending++;
  }
  xSemaphoreGive(slot->lock);
  publish_overflow_if_due(slot, false);
}

static void deliver_rx(esp32s3_uart_slot_t* slot, uint32_t msg_len, uint32_t* local_flush_seq) {
  if (msg_len == 0 || msg_len > CMD_FRAME_MAX) {
    return;
  }
  xSemaphoreTake(slot->lock, portMAX_DELAY);
  const uint32_t seq = __atomic_load_n(&slot->rx_flush_seq, __ATOMIC_ACQUIRE);
  if (seq != *local_flush_seq) {
    *local_flush_seq = seq;
    xSemaphoreGive(slot->lock);
    return;  // FLUSH / TRANSACT cleared input while this message was finishing
  }
  const sys_uart_rx_cb_f cb = slot->hook.cb;
  void* ctx = slot->hook.ctx;
  const uint8_t sub_id = slot->sub_id;
  const uint8_t sub_gen = slot->sub_gen;
  const uint16_t sub_op = slot->sub_op;
  const uint8_t dev_id = slot->dev_id;
  bool rb_overflow = false;
  if (cb == NULL && sub_id == SYS_DEVICE_ID_NONE) {
    if (xRingbufferSend(slot->rx_rb, slot->rx_pkt.data, msg_len, 0) != pdTRUE) {
      if (slot->ovf_pending < UINT16_MAX) {
        slot->ovf_pending++;
      }
      rb_overflow = true;
    }
  }
  xSemaphoreGive(slot->lock);

  if (rb_overflow) {
    publish_overflow_if_due(slot, false);
    return;
  }
  if (cb != NULL) {
    cb(ctx, slot->rx_pkt.data, msg_len);
  } else if (sub_id != SYS_DEVICE_ID_NONE) {
    slot->rx_pkt.hdr.port = dev_id;
    err_report(sys_device_deliver(sub_id, sub_gen, sub_op, &slot->rx_pkt,
                                  (uint32_t)sizeof(slot->rx_pkt.hdr) + msg_len));
  }
}

static void process_hook_pending(esp32s3_uart_slot_t* slot) {
  sys_uart_rx_cb_f cb = NULL;
  void* ctx = NULL;
  xSemaphoreTake(slot->lock, portMAX_DELAY);
  if (slot->hook_notify_up && slot->hook.cb != NULL) {
    cb = slot->hook.cb;
    ctx = slot->hook.ctx;
    slot->hook_notify_up = false;
  }
  xSemaphoreGive(slot->lock);
  if (cb != NULL) {
    cb(ctx, NULL, 1u);
  }
}

static void feed_framed(esp32s3_uart_slot_t* slot, uint32_t* cand_len, bool* cand_ovf, uint32_t* flush_seq,
                        const uint8_t* buf, size_t n) {
  for (size_t i = 0; i < n; i++) {
    const uint8_t b = buf[i];
    if (b == 0) {
      if (*cand_len > 0 && !*cand_ovf) {
        const uint32_t dec = sys_uart_cobs_decode(slot->rx_pkt.data, *cand_len);
        if (dec > ESP32S3_UART_CRC_BYTES && (dec - ESP32S3_UART_CRC_BYTES) <= CMD_FRAME_MAX) {
          const uint32_t msg_len = dec - ESP32S3_UART_CRC_BYTES;
          const uint16_t crc = (uint16_t)(slot->rx_pkt.data[msg_len] | (slot->rx_pkt.data[msg_len + 1u] << 8));
          if (sys_uart_crc16(SYS_UART_CRC16_INIT, slot->rx_pkt.data, msg_len) == crc) {
            deliver_rx(slot, msg_len, flush_seq);
          }
        }
      }
      *cand_len = 0;
      *cand_ovf = false;
    } else if (*cand_len < sizeof(slot->rx_pkt.data)) {
      slot->rx_pkt.data[(*cand_len)++] = b;
    } else {
      *cand_ovf = true;
    }
  }
}

static void feed_raw_line(esp32s3_uart_slot_t* slot, uint32_t* cand_len, uint32_t* flush_seq, const uint8_t* buf,
                          size_t n) {
  for (size_t i = 0; i < n; i++) {
    const uint8_t b = buf[i];
    if (b == '\n') {
      if (*cand_len > 0 && slot->rx_pkt.data[*cand_len - 1u] == '\r') {
        (*cand_len)--;
      }
      if (*cand_len > 0) {
        deliver_rx(slot, *cand_len, flush_seq);
        *cand_len = 0;
      }
    } else {
      slot->rx_pkt.data[(*cand_len)++] = b;
      if (*cand_len >= CMD_FRAME_MAX) {
        deliver_rx(slot, CMD_FRAME_MAX, flush_seq);
        *cand_len = 0;
      }
    }
  }
}

static void feed_raw(esp32s3_uart_slot_t* slot, uint32_t* cand_len, uint32_t* flush_seq, const uint8_t* buf, size_t n) {
  for (size_t i = 0; i < n; i++) {
    slot->rx_pkt.data[(*cand_len)++] = buf[i];
    if (*cand_len >= CMD_FRAME_MAX) {
      deliver_rx(slot, CMD_FRAME_MAX, flush_seq);
      *cand_len = 0;
    }
  }
}

static void handle_uart_data(esp32s3_uart_slot_t* slot, size_t evt_size, bool timeout_flag, uint32_t* cand_len,
                             bool* cand_ovf, uint32_t* flush_seq) {
  size_t rem = evt_size;
  while (rem > 0 && !__atomic_load_n(&slot->stopping, __ATOMIC_ACQUIRE)) {
    const size_t want = rem < sizeof(slot->rx_chunk) ? rem : sizeof(slot->rx_chunk);
    const int got = uart_read_bytes(slot->uart, slot->rx_chunk, want, 0);
    if (got <= 0) {
      break;
    }
    rem -= (size_t)got;
    if (slot->mode == SYS_UART_MODE_FRAMED) {
      feed_framed(slot, cand_len, cand_ovf, flush_seq, slot->rx_chunk, (size_t)got);
    } else if (slot->mode == SYS_UART_MODE_RAW_LINE) {
      feed_raw_line(slot, cand_len, flush_seq, slot->rx_chunk, (size_t)got);
    } else {
      feed_raw(slot, cand_len, flush_seq, slot->rx_chunk, (size_t)got);
    }
  }
  if (slot->mode == SYS_UART_MODE_RAW && timeout_flag && *cand_len > 0) {
    size_t buffered = 0;
    if (uart_get_buffered_data_len(slot->uart, &buffered) == ESP_OK && buffered == 0) {
      deliver_rx(slot, *cand_len, flush_seq);
      *cand_len = 0;
    }
  }
}

static void rx_task_fn(void* arg) {
  esp32s3_uart_slot_t* slot = arg;
  for (;;) {
    if (__atomic_load_n(&slot->stopping, __ATOMIC_ACQUIRE)) {
      xSemaphoreGive(slot->parked_sem);
      while (__atomic_load_n(&slot->stopping, __ATOMIC_ACQUIRE)) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      }
    }
    uint32_t cand_len = 0;
    bool cand_ovf = false;
    uint32_t local_flush_seq = __atomic_load_n(&slot->rx_flush_seq, __ATOMIC_ACQUIRE);

    while (!__atomic_load_n(&slot->stopping, __ATOMIC_ACQUIRE)) {
      process_hook_pending(slot);
      publish_overflow_if_due(slot, false);
      uart_event_t evt;
      const TickType_t wait = rx_event_wait_ticks(slot);
      if (xQueueReceive(slot->evt_queue, &evt, wait) != pdTRUE) {
        continue;
      }
      if (__atomic_load_n(&slot->stopping, __ATOMIC_ACQUIRE)) {
        break;
      }
      const uint32_t seq = __atomic_load_n(&slot->rx_flush_seq, __ATOMIC_ACQUIRE);
      if (seq != local_flush_seq) {
        local_flush_seq = seq;
        cand_len = 0;
        cand_ovf = false;
      }
      switch (evt.type) {
        case UART_DATA:
          handle_uart_data(slot, evt.size, evt.timeout_flag, &cand_len, &cand_ovf, &local_flush_seq);
          break;
        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
          (void)uart_flush_input(slot->uart);
          (void)xQueueReset(slot->evt_queue);
          cand_len = 0;
          cand_ovf = slot->mode == SYS_UART_MODE_FRAMED;  // wait cleanly for next 0x00 delimiter
          record_overflow(slot);
          break;
        case UART_FRAME_ERR:
        case UART_PARITY_ERR:
        case UART_BREAK:
          if (slot->mode == SYS_UART_MODE_FRAMED) {
            cand_len = 0;
            cand_ovf = false;
          }
          break;
        default:
          break;
      }
    }
  }
}

/* ----------------------------------------------------------------------------
 * Static slot lifecycle (claim, open, reset, release)
 * ------------------------------------------------------------------------- */

static void slot_init_once(esp32s3_uart_slot_t* slot) {
  if (slot->rtos_inited) {
    return;
  }
  slot->lock = xSemaphoreCreateMutexStatic(&slot->lock_buf);
  slot->parked_sem = xSemaphoreCreateCountingStatic(2, 0, &slot->parked_buf);
  slot->tx_rb = xRingbufferCreateStatic(sizeof(slot->tx_rb_mem), RINGBUF_TYPE_NOSPLIT, slot->tx_rb_mem, &slot->tx_rb_buf);
  slot->urgent_rb =
      xRingbufferCreateStatic(sizeof(slot->urgent_rb_mem), RINGBUF_TYPE_NOSPLIT, slot->urgent_rb_mem, &slot->urgent_rb_buf);
  slot->rx_rb = xRingbufferCreateStatic(sizeof(slot->rx_rb_mem), RINGBUF_TYPE_NOSPLIT, slot->rx_rb_mem, &slot->rx_rb_buf);
  slot->rtos_inited = true;
}

static void start_slot_tasks(esp32s3_uart_slot_t* slot) {
  const uint32_t idx = (uint32_t)(slot - s_slots);
  __atomic_store_n(&slot->stopping, false, __ATOMIC_RELEASE);

  if (slot->tx_task == NULL) {
    char name[16];
    (void)snprintf(name, sizeof(name), "uart_tx%lu", (unsigned long)idx);
    slot->tx_task = xTaskCreateStatic(tx_task_fn, name, sizeof(slot->tx_stack) / sizeof(StackType_t), slot,
                                      CONFIG_ESP32S3_UART_TX_TASK_PRIO, slot->tx_stack, &slot->tx_tcb);
  } else {
    xTaskNotifyGive(slot->tx_task);
  }
  slot->tx_running = true;

  if (slot->rx_task == NULL) {
    char name[16];
    (void)snprintf(name, sizeof(name), "uart_rx%lu", (unsigned long)idx);
    slot->rx_task = xTaskCreateStatic(rx_task_fn, name, sizeof(slot->rx_stack) / sizeof(StackType_t), slot,
                                      CONFIG_ESP32S3_UART_RX_TASK_PRIO, slot->rx_stack, &slot->rx_tcb);
  } else {
    xTaskNotifyGive(slot->rx_task);
  }
  slot->rx_running = true;
}

static void stop_slot_tasks(esp32s3_uart_slot_t* slot) {
  const uint32_t running = (uint32_t)slot->tx_running + (uint32_t)slot->rx_running;
  if (running == 0) {
    return;
  }
  while (xSemaphoreTake(slot->parked_sem, 0) == pdTRUE) {
  }
  __atomic_store_n(&slot->stopping, true, __ATOMIC_RELEASE);
  if (slot->hw_installed) {
    (void)uart_set_hw_flow_ctrl(slot->uart, UART_HW_FLOWCTRL_DISABLE, 0);
    (void)uart_disable_rx_intr(slot->uart);
  }
  if (slot->tx_running && slot->tx_task != NULL) {
    xTaskNotifyGive(slot->tx_task);
  }
  if (slot->rx_running && slot->evt_queue != NULL) {
    const uart_event_t stop_ev = {.type = UART_EVENT_MAX};
    (void)xQueueSendToFront(slot->evt_queue, &stop_ev, MSEC(100));
  }
  uint32_t parked = 0;
  while (parked < running && xSemaphoreTake(slot->parked_sem, MSEC(PARK_WAIT_MS)) == pdTRUE) {
    parked++;
  }
  slot->tx_running = false;
  slot->rx_running = false;
}

err_h esp32s3_uart_slot_claim(uint8_t dev_id, uint8_t uart_num, esp32s3_uart_slot_t** out_slot) {
  esp32s3_uart_slot_t* free_slot = NULL;
  {
    U_MUTEX_GUARD(s_slots_lock);
    for (uint32_t i = 0; i < CONFIG_ESP32S3_UART_MAX_PORTS; i++) {
      if (s_slots[i].in_use && s_slots[i].uart == (uart_port_t)uart_num) {
        return ERR_NEW(ERR_UART_IN_USE, .port = dev_id, .owner = s_slots[i].dev_id);
      }
    }
    ERR_CHECK(!uart_is_driver_installed((uart_port_t)uart_num), ERR_UART_IN_USE, .port = dev_id,
              .owner = SYS_DEVICE_ID_NONE);
    for (uint32_t i = 0; i < CONFIG_ESP32S3_UART_MAX_PORTS; i++) {
      if (!s_slots[i].in_use) {
        free_slot = &s_slots[i];
        break;
      }
    }
    ERR_CHECK(free_slot != NULL, ERR_DEV_NO_MEM, .id = dev_id, .size = (uint16_t)sizeof(esp32s3_uart_slot_t));
    slot_init_once(free_slot);
    free_slot->in_use = true;
    free_slot->dev_id = dev_id;
    free_slot->uart = (uart_port_t)uart_num;
    free_slot->hw_installed = false;
    free_slot->tx_running = false;
    free_slot->rx_running = false;
    free_slot->evt_queue = NULL;
    free_slot->sub_id = SYS_DEVICE_ID_NONE;
    free_slot->sub_gen = 0;
    free_slot->sub_op = 0;
    free_slot->hook = (esp32s3_uart_hook_t){0};
    free_slot->hook_notify_up = false;
    free_slot->rx_reader_busy = false;
    free_slot->rx_flush_seq = 0;
    free_slot->ovf_pending = 0;
    free_slot->ovf_sent_once = false;
    free_slot->last_ovf_tick = 0;
  }
  esp32s3_uart_drain_ring(free_slot->tx_rb);
  esp32s3_uart_drain_ring(free_slot->urgent_rb);
  esp32s3_uart_drain_ring(free_slot->rx_rb);
  *out_slot = free_slot;
  return NULL;
}

err_h esp32s3_uart_slot_open(esp32s3_uart_slot_t* slot, const esp32s3_uart_cfg_t* cfg) {
  slot->mode = cfg->mode;
  slot->baud = cfg->baud;
  slot->init_baud = cfg->baud;
  slot->parity = cfg->parity;
  slot->stop_bits = cfg->stop_bits;

  int intr = 0;
#if CONFIG_UART_ISR_IN_IRAM
  intr |= ESP_INTR_FLAG_IRAM;
#endif
  esp_err_t rc = uart_driver_install(slot->uart, CONFIG_ESP32S3_UART_ISR_RX_BUF, 0, CONFIG_ESP32S3_UART_EVT_QUEUE_LEN,
                                     &slot->evt_queue, intr);
  if (rc == ESP_OK) {
    slot->hw_installed = true;
    const uart_config_t ucfg = {
        .baud_rate = (int)cfg->baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = idf_parity(cfg->parity),
        .stop_bits = idf_stop_bits(cfg->stop_bits),
        .flow_ctrl = idf_flow_ctrl(cfg->rts, cfg->cts),
        .rx_flow_ctrl_thresh = RX_FLOW_THRESH,
        .source_clk = UART_SCLK_DEFAULT,
    };
    rc = uart_param_config(slot->uart, &ucfg);
  }
  if (rc == ESP_OK) {
    const int tx_pin = sys_io_pin_valid(cfg->tx) ? (int)cfg->tx.pin : UART_PIN_NO_CHANGE;
    const int rx_pin = sys_io_pin_valid(cfg->rx) ? (int)cfg->rx.pin : UART_PIN_NO_CHANGE;
    const int rts_pin = sys_io_pin_valid(cfg->rts) ? (int)cfg->rts.pin : UART_PIN_NO_CHANGE;
    const int cts_pin = sys_io_pin_valid(cfg->cts) ? (int)cfg->cts.pin : UART_PIN_NO_CHANGE;
    rc = uart_set_pin(slot->uart, tx_pin, rx_pin, rts_pin, cts_pin);
  }
  if (rc == ESP_OK) {
    rc = uart_set_rx_timeout(slot->uart, RX_TIMEOUT_SYMS);
  }
  ERR_CHECK(rc == ESP_OK, ERR_UART_INIT, .port = slot->dev_id);
  uart_set_always_rx_timeout(slot->uart, true);

  start_slot_tasks(slot);
  return NULL;
}

void esp32s3_uart_slot_release(esp32s3_uart_slot_t* slot) {
  if (slot == NULL) {
    return;
  }
  sys_uart_rx_cb_f down_cb = NULL;
  void* down_ctx = NULL;
  xSemaphoreTake(slot->lock, portMAX_DELAY);
  down_cb = slot->hook.cb;
  down_ctx = slot->hook.ctx;
  slot->hook = (esp32s3_uart_hook_t){0};
  slot->hook_notify_up = false;
  slot->sub_id = SYS_DEVICE_ID_NONE;
  slot->sub_gen = 0;
  slot->sub_op = 0;
  slot->rx_reader_busy = false;
  xSemaphoreGive(slot->lock);

  stop_slot_tasks(slot);
  if (down_cb != NULL) {
    down_cb(down_ctx, NULL, 0u);  // RX task already parked: no data callback can race after down
  }
  if (slot->hw_installed) {
    (void)uart_driver_delete(slot->uart);
    slot->hw_installed = false;
    slot->evt_queue = NULL;
  }
  esp32s3_uart_drain_ring(slot->tx_rb);
  esp32s3_uart_drain_ring(slot->urgent_rb);
  esp32s3_uart_drain_ring(slot->rx_rb);

  U_MUTEX_GUARD(s_slots_lock);
  slot->in_use = false;
}

err_h esp32s3_uart_slot_reset(esp32s3_uart_slot_t* slot) {
  (void)uart_wait_tx_done(slot->uart, MSEC(100));
  ERR_TRY(ERR_ESP(uart_set_baudrate(slot->uart, slot->init_baud)));
  xSemaphoreTake(slot->lock, portMAX_DELAY);
  slot->baud = slot->init_baud;
  esp32s3_uart_drain_ring(slot->tx_rb);
  esp32s3_uart_drain_ring(slot->urgent_rb);
  if (!slot->rx_reader_busy) {
    esp32s3_uart_flush_rx_locked(slot);
  } else {
    __atomic_add_fetch(&slot->rx_flush_seq, 1u, __ATOMIC_RELEASE);
    (void)uart_flush_input(slot->uart);
  }
  xSemaphoreGive(slot->lock);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Shared TX / RX data helpers (used by OP_SEND, OP_SEND_FRAME, OP_RECEIVE, OP_TRANSACT)
 * ------------------------------------------------------------------------- */

static err_h auth_send_locked(const esp32s3_uart_slot_t* slot, uint8_t caller, bool has_token, const void* token) {
  if (slot->sub_id != SYS_DEVICE_ID_NONE) {
    ERR_CHECK(caller == slot->sub_id, ERR_UART_IN_USE, .port = slot->dev_id, .owner = slot->sub_id);
    return NULL;
  }
  if (slot->hook.cb != NULL) {
    if (slot->mode != SYS_UART_MODE_FRAMED && slot->hook.allow_send) {
      return NULL;
    }
    ERR_CHECK(has_token && token == slot->hook.ctx, ERR_UART_IN_USE, .port = slot->dev_id,
              .owner = SYS_DEVICE_ID_NONE);
    return NULL;
  }
  return NULL;
}

err_h esp32s3_uart_send(esp32s3_uart_slot_t* slot, uint8_t caller, cmd_in_t frame, uint16_t wait_ms, uint8_t flags,
                        bool has_token, const void* token) {
  ERR_CHECK(frame.len > 0 && frame.len <= CMD_FRAME_MAX && frame.data != NULL, ERR_UART_BAD_ARG);
  ERR_CHECK(wait_ms <= SYS_UART_WAIT_MAX_MS, ERR_UART_BAD_ARG);
  ERR_CHECK((flags & ~(CMD_SEND_URGENT | CMD_SEND_SPARE | CMD_SEND_NOWAIT)) == 0, ERR_UART_BAD_ARG);

  xSemaphoreTake(slot->lock, portMAX_DELAY);
  const err_h auth_err = auth_send_locked(slot, caller, has_token, token);
  xSemaphoreGive(slot->lock);
  ERR_TRY(auth_err);

  const bool framed = slot->mode == SYS_UART_MODE_FRAMED;
  const uint32_t item_len = framed ? (frame.len + ESP32S3_UART_CRC_BYTES) : frame.len;
  const bool urgent = (flags & CMD_SEND_URGENT) != 0;
  const RingbufHandle_t rb = urgent ? slot->urgent_rb : slot->tx_rb;
  const uint32_t cap = urgent ? ESP32S3_UART_URGENT_QUEUE : CONFIG_ESP32S3_UART_TX_QUEUE;

  if (flags & CMD_SEND_SPARE) {
    const uint32_t used = ring_used(rb, cap);
    ERR_CHECK(used + ESP32S3_UART_ITEM_BYTES(item_len) <= cap / 2u, ERR_UART_BUSY, .port = slot->dev_id);
  }
  const TickType_t wait = (wait_ms == 0 || (flags & CMD_SEND_NOWAIT) != 0) ? 0 : MSEC(wait_ms);
  uint8_t* item = NULL;
  ERR_CHECK(xRingbufferSendAcquire(rb, (void**)&item, item_len, wait) == pdTRUE, ERR_UART_BUSY,
            .port = slot->dev_id);
  memcpy(item, frame.data, frame.len);
  if (framed) {
    const uint16_t crc = sys_uart_crc16(SYS_UART_CRC16_INIT, frame.data, frame.len);
    item[frame.len] = (uint8_t)crc;
    item[frame.len + 1u] = (uint8_t)(crc >> 8);
  }
  xRingbufferSendComplete(rb, item);
  xTaskNotifyGive(slot->tx_task);
  return NULL;
}

err_h esp32s3_uart_receive_one(esp32s3_uart_slot_t* slot, TickType_t wait, cmd_out_t* out) {
  size_t item_sz = 0;
  void* item = xRingbufferReceive(slot->rx_rb, &item_sz, wait);
  if (item == NULL) {
    return NULL;
  }
  const err_h err = cmd_reply(out, item, (uint32_t)item_sz);
  vRingbufferReturnItem(slot->rx_rb, item);
  return err;
}
