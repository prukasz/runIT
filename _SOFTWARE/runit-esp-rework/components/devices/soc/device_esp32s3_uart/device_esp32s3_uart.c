/*
 * ESP32-S3 UART controller as a device, type 0x004D: implements the UART contract (sys_uart_contract.h) on
 * esp_driver_uart, completely isolated from sys_data_provider.
 *
 * Static allocation: ESP32-S3 has SOC_UART_NUM (3) physical UARTs. Rather than heap-allocating tasks, ring buffers,
 * and 520 B line buffers inside SYS_DEVICE_CTX (which fragments heap on REMOVE/CREATE), up to
 * CONFIG_ESP32S3_UART_MAX_PORTS engine slots live statically in .bss (esp32s3_uart_slot.c). Static tasks are created
 * once on first slot claim and parked between REMOVE and the next CREATE; SYS_DEVICE_CTX only holds pin claims,
 * resource handle, and slot ref.
 */

#include "driver/gpio.h"
#include "esp32s3_uart.h"
#include "sys_io_driver.h"

SYS_DEVICE_TYPE_DECLARE(ESP32S3_UART, 0x004D)

typedef struct {
  uint8_t id;
  esp32s3_uart_slot_t* slot;
  sys_io_claim_t tx;
  sys_io_claim_t rx;
  sys_io_claim_t rts;
  sys_io_claim_t cts;
  sys_device_res_t uart_res;
} esp_uart_dev_t;

#define SYS_DEVICE_CTX esp_uart_dev_t

/* ----------------------------------------------------------------------------
 * Contract operations (CONTRACT_UART)
 * ------------------------------------------------------------------------- */

OP_DECLARE_READ(CONTRACT_UART, OP_INFO) {
  esp32s3_uart_slot_t* slot = c->slot;
  xSemaphoreTake(slot->lock, portMAX_DELAY);
  const sys_uart_info_t info = {
      .mode = slot->mode,
      .baud = slot->baud,
      .parity = slot->parity,
      .stop_bits = slot->stop_bits,
  };
  xSemaphoreGive(slot->lock);
  return cmd_reply(out, &info, sizeof(info));
}

OP_DECLARE_READ(CONTRACT_UART, OP_BAUD) {
  esp32s3_uart_slot_t* slot = c->slot;
  xSemaphoreTake(slot->lock, portMAX_DELAY);
  const uint32_t baud = slot->baud;
  xSemaphoreGive(slot->lock);
  return cmd_reply(out, &baud, sizeof(baud));
}

OP_DECLARE_WRITE(CONTRACT_UART, OP_BAUD) {
  esp32s3_uart_slot_t* slot = c->slot;
  ERR_CHECK(*args >= SYS_UART_BAUD_MIN && *args <= SYS_UART_BAUD_MAX, ERR_UART_BAD_BAUD, .baud = *args);
  (void)uart_wait_tx_done(slot->uart, MSEC(100));
  ERR_TRY(ERR_ESP(uart_set_baudrate(slot->uart, *args)));
  xSemaphoreTake(slot->lock, portMAX_DELAY);
  slot->baud = *args;
  xSemaphoreGive(slot->lock);
  return NULL;
}

OP_DECLARE_WRITE_TAIL(CONTRACT_UART, OP_SEND) {
  return esp32s3_uart_send(c->slot, sys_device_caller(), (cmd_in_t){tail, tail_len}, args->wait_ms, args->flags, false,
                           NULL);
}

OP_DECLARE_WRITE(CONTRACT_UART, OP_SEND_FRAME) {
  return esp32s3_uart_send(c->slot, sys_device_caller(), args->frame, args->wait_ms, args->flags, true, args->token);
}

OP_DECLARE_READ(CONTRACT_UART, OP_RECEIVE) {
  esp32s3_uart_slot_t* slot = c->slot;
  ERR_CHECK(args->wait_ms <= SYS_UART_WAIT_MAX_MS, ERR_UART_BAD_ARG);

  xSemaphoreTake(slot->lock, portMAX_DELAY);
  uint8_t owner = SYS_DEVICE_ID_NONE;
  if (esp32s3_uart_has_reader_locked(slot, &owner)) {
    xSemaphoreGive(slot->lock);
    return ERR_NEW(ERR_UART_IN_USE, .port = c->id, .owner = owner);
  }
  slot->rx_reader_busy = true;
  xSemaphoreGive(slot->lock);

  const TickType_t wait = args->wait_ms == 0 ? 0 : MSEC(args->wait_ms);
  const err_h err = esp32s3_uart_receive_one(slot, wait, out);

  xSemaphoreTake(slot->lock, portMAX_DELAY);
  slot->rx_reader_busy = false;
  xSemaphoreGive(slot->lock);
  return err;
}

OP_DECLARE_WRITE(CONTRACT_UART, OP_FLUSH) {
  esp32s3_uart_slot_t* slot = c->slot;
  xSemaphoreTake(slot->lock, portMAX_DELAY);
  uint8_t owner = SYS_DEVICE_ID_NONE;
  if (esp32s3_uart_has_reader_locked(slot, &owner)) {
    xSemaphoreGive(slot->lock);
    return ERR_NEW(ERR_UART_IN_USE, .port = c->id, .owner = owner);
  }
  esp32s3_uart_flush_rx_locked(slot);
  xSemaphoreGive(slot->lock);
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_UART, OP_SUBSCRIBE) {
  esp32s3_uart_slot_t* slot = c->slot;
  const uint8_t caller = sys_device_caller();
  xSemaphoreTake(slot->lock, portMAX_DELAY);

  if (args->op == 0) {
    if (slot->sub_id == SYS_DEVICE_ID_NONE) {
      if (slot->hook.cb != NULL || slot->rx_reader_busy) {
        xSemaphoreGive(slot->lock);
        return ERR_NEW(ERR_UART_IN_USE, .port = c->id, .owner = SYS_DEVICE_ID_NONE);
      }
      xSemaphoreGive(slot->lock);
      return NULL;
    }
    if (caller != slot->sub_id) {
      const uint8_t owner = slot->sub_id;
      xSemaphoreGive(slot->lock);
      return ERR_NEW(ERR_UART_IN_USE, .port = c->id, .owner = owner);
    }
    slot->sub_id = SYS_DEVICE_ID_NONE;
    slot->sub_gen = 0;
    slot->sub_op = 0;
    xSemaphoreGive(slot->lock);
    return NULL;
  }

  const uint8_t caller_gen = caller < SYS_DEVICE_COUNT ? sys_device_gen(caller) : 0u;
  if (caller >= SYS_DEVICE_COUNT || caller_gen == 0u || (args->op >> 8) != SYS_DEVICE_CONTRACT_OWN) {
    xSemaphoreGive(slot->lock);
    return ERR_NEW(ERR_UART_BAD_ARG);
  }
  uint8_t owner = SYS_DEVICE_ID_NONE;
  if (esp32s3_uart_has_reader_locked(slot, &owner)) {
    xSemaphoreGive(slot->lock);
    return ERR_NEW(ERR_UART_IN_USE, .port = c->id, .owner = owner);
  }
  slot->sub_id = caller;
  slot->sub_gen = caller_gen;
  slot->sub_op = args->op;
  esp32s3_uart_drain_ring(slot->rx_rb);
  xSemaphoreGive(slot->lock);
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_UART, OP_RX_HOOK) {
  esp32s3_uart_slot_t* slot = c->slot;
  xSemaphoreTake(slot->lock, portMAX_DELAY);

  if (args->cb == NULL) {
    if (slot->sub_id != SYS_DEVICE_ID_NONE) {
      const uint8_t owner = slot->sub_id;
      xSemaphoreGive(slot->lock);
      return ERR_NEW(ERR_UART_IN_USE, .port = c->id, .owner = owner);
    }
    if (slot->rx_reader_busy) {
      xSemaphoreGive(slot->lock);
      return ERR_NEW(ERR_UART_IN_USE, .port = c->id, .owner = SYS_DEVICE_ID_NONE);
    }
    if (slot->hook.cb != NULL) {
      if (args->ctx != slot->hook.ctx) {
        xSemaphoreGive(slot->lock);
        return ERR_NEW(ERR_UART_IN_USE, .port = c->id, .owner = SYS_DEVICE_ID_NONE);
      }
      slot->hook = (esp32s3_uart_hook_t){0};
      slot->hook_notify_up = false;
    }
    xSemaphoreGive(slot->lock);
    return NULL;
  }

  uint8_t owner = SYS_DEVICE_ID_NONE;
  if (esp32s3_uart_has_reader_locked(slot, &owner)) {
    xSemaphoreGive(slot->lock);
    return ERR_NEW(ERR_UART_IN_USE, .port = c->id, .owner = owner);
  }
  slot->hook = (esp32s3_uart_hook_t){.cb = args->cb, .ctx = args->ctx, .allow_send = args->allow_send};
  slot->hook_notify_up = true;
  esp32s3_uart_drain_ring(slot->rx_rb);
  xSemaphoreGive(slot->lock);
  esp32s3_uart_notify_rx_wakeup(slot);
  return NULL;
}

OP_DECLARE_WRITE_TAIL(CONTRACT_UART, OP_TRANSACT) {
  esp32s3_uart_slot_t* slot = c->slot;
  ERR_CHECK(args->wait_ms <= SYS_UART_WAIT_MAX_MS, ERR_UART_BAD_ARG);
  ERR_CHECK(tail_len > 0 && tail_len <= CMD_FRAME_MAX, ERR_UART_BAD_ARG);

  xSemaphoreTake(slot->lock, portMAX_DELAY);
  uint8_t owner = SYS_DEVICE_ID_NONE;
  if (esp32s3_uart_has_reader_locked(slot, &owner)) {
    xSemaphoreGive(slot->lock);
    return ERR_NEW(ERR_UART_IN_USE, .port = c->id, .owner = owner);
  }
  slot->rx_reader_busy = true;
  esp32s3_uart_flush_rx_locked(slot);
  xSemaphoreGive(slot->lock);

  const TickType_t start = xTaskGetTickCount();
  const TickType_t total_ticks = args->wait_ms == 0 ? 0 : MSEC(args->wait_ms);
  const TickType_t deadline = start + total_ticks;

  err_h err = esp32s3_uart_send(slot, sys_device_caller(), (cmd_in_t){tail, tail_len}, args->wait_ms, 0u, false, NULL);
  if (err == NULL) {
    const TickType_t now = xTaskGetTickCount();
    const TickType_t elapsed = now - start;
    const TickType_t rem = elapsed >= total_ticks ? 0 : (deadline - now);
    err = esp32s3_uart_receive_one(slot, rem, out);
  }

  xSemaphoreTake(slot->lock, portMAX_DELAY);
  slot->rx_reader_busy = false;
  xSemaphoreGive(slot->lock);
  return err;
}

/* ----------------------------------------------------------------------------
 * Lifecycle (reset, suspend, resume, create, release)
 * ------------------------------------------------------------------------- */

OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  return esp32s3_uart_slot_reset(c->slot);
}

static err_h suspend(esp_uart_dev_t* c) {
  (void)c;
  return NULL;  // SYS_UART_CONTRACT: port keeps running during safe state so link stays reachable
}

static err_h resume(esp_uart_dev_t* c) {
  (void)c;
  return NULL;
}

static err_h uart_release(void* arg) {
  esp_uart_dev_t* c = arg;
  esp32s3_uart_slot_release(c->slot);
  c->slot = NULL;
  return NULL;
}

static err_h validate_cfg(uint8_t id, const esp32s3_uart_cfg_t* cfg) {
  ERR_CHECK(cfg->uart_num < SOC_UART_NUM, ERR_DEV_RANGE, .id = id, .value = cfg->uart_num, .min = 0,
            .max = SOC_UART_NUM - 1);
  ERR_CHECK(cfg->mode <= SYS_UART_MODE_RAW_LINE, ERR_UART_BAD_ARG);
  ERR_CHECK(cfg->baud >= SYS_UART_BAUD_MIN && cfg->baud <= SYS_UART_BAUD_MAX, ERR_UART_BAD_BAUD, .baud = cfg->baud);
  ERR_CHECK(cfg->parity <= SYS_UART_PARITY_ODD, ERR_DEV_RANGE, .id = id, .value = cfg->parity, .min = 0,
            .max = SYS_UART_PARITY_ODD);
  ERR_CHECK(cfg->stop_bits == 1u || cfg->stop_bits == 2u, ERR_DEV_RANGE, .id = id, .value = cfg->stop_bits, .min = 1,
            .max = 2);
  ERR_CHECK(sys_io_pin_valid(cfg->tx) || sys_io_pin_valid(cfg->rx), ERR_UART_BAD_ARG);
  if (sys_io_pin_valid(cfg->tx)) {
    ERR_CHECK(GPIO_IS_VALID_OUTPUT_GPIO(cfg->tx.pin), ERR_DEV_BAD_PIN, .id = id, .pin = cfg->tx.pin);
  }
  if (sys_io_pin_valid(cfg->rx)) {
    ERR_CHECK(GPIO_IS_VALID_GPIO(cfg->rx.pin), ERR_DEV_BAD_PIN, .id = id, .pin = cfg->rx.pin);
  }
  if (sys_io_pin_valid(cfg->rts)) {
    ERR_CHECK(GPIO_IS_VALID_OUTPUT_GPIO(cfg->rts.pin), ERR_DEV_BAD_PIN, .id = id, .pin = cfg->rts.pin);
  }
  if (sys_io_pin_valid(cfg->cts)) {
    ERR_CHECK(GPIO_IS_VALID_GPIO(cfg->cts.pin), ERR_DEV_BAD_PIN, .id = id, .pin = cfg->cts.pin);
  }
  return NULL;
}

static err_h create(uint8_t id, esp_uart_dev_t* c, const esp32s3_uart_cfg_t* cfg) {
  c->id = id;
  ERR_TRY(validate_cfg(id, cfg));

  ERR_TRY(sys_io_claim_peripheral(id, &c->tx, cfg->tx));
  ERR_TRY(sys_io_claim_peripheral(id, &c->rx, cfg->rx));
  ERR_TRY(sys_io_claim_peripheral(id, &c->rts, cfg->rts));
  ERR_TRY(sys_io_claim_peripheral(id, &c->cts, cfg->cts));

  esp32s3_uart_slot_t* slot = NULL;
  ERR_TRY(esp32s3_uart_slot_claim(id, cfg->uart_num, &slot));
  c->slot = slot;

  const err_h res_err = sys_device_res_add(id, &c->uart_res, uart_release, c);
  if (res_err != NULL) {
    esp32s3_uart_slot_release(slot);
    c->slot = NULL;
    return res_err;
  }
  return esp32s3_uart_slot_open(slot, cfg);
}

/* ----------------------------------------------------------------------------
 * Type registration
 * ------------------------------------------------------------------------- */

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),
    OP_ENTRY_R(CONTRACT_UART, OP_INFO),
    OP_ENTRY(CONTRACT_UART, OP_BAUD),
    OP_ENTRY_W(CONTRACT_UART, OP_SEND),
    OP_ENTRY_R(CONTRACT_UART, OP_RECEIVE),
    OP_ENTRY_W(CONTRACT_UART, OP_FLUSH),
    OP_ENTRY_W(CONTRACT_UART, OP_SUBSCRIBE),
    OP_ENTRY_W(CONTRACT_UART, OP_TRANSACT),
    OP_ENTRY_W(CONTRACT_UART, OP_SEND_FRAME),
    OP_ENTRY_W(CONTRACT_UART, OP_RX_HOOK),
};

SYS_DEVICE_TYPE_DEFINE(ESP32S3_UART, esp32s3_uart_cfg_t, create, suspend, resume, k_ops)
