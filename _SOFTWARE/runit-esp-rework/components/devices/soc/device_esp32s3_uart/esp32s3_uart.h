#pragma once

#include "driver/uart.h"
#include "sys_io.h"
#include "sys_uart_codec.h"
#include "sys_uart_contract.h"
#include "utils.h"

/*
 * Private to device_esp32s3_uart, nothing else includes it (no INCLUDE_DIRS):
 * - device_esp32s3_uart.c: device registration, CREATE config validation, and CONTRACT_UART / RESET ops.
 * - esp32s3_uart_slot.c:   static slot pool in .bss, IDF driver setup/teardown, TX/RX tasks, and ring helpers.
 */

#define ESP32S3_UART_CRC_BYTES      SYS_UART_CRC_BYTES
#define ESP32S3_UART_RAW_MAX        (CMD_FRAME_MAX + ESP32S3_UART_CRC_BYTES)  // [frame][crc16] in FRAMED rings
#define ESP32S3_UART_COBS_MAX       SYS_UART_COBS_MAX(ESP32S3_UART_RAW_MAX)
#define ESP32S3_UART_ITEM_BYTES(n)  ((((uint32_t)(n) + 3u) & ~3u) + 8u)  // NOSPLIT ring overhead per item
#define ESP32S3_UART_URGENT_QUEUE   (ESP32S3_UART_ITEM_BYTES(ESP32S3_UART_RAW_MAX) * 2u)
#define ESP32S3_UART_RX_CHUNK       128u

/** @brief Wire configuration passed to SYS_DEVICE_CREATE for ESP32S3_UART. */
typedef struct __attribute__((packed)) {
  uint8_t uart_num;   /**< Controller 0..SOC_UART_NUM-1 (UART0 refused if console uses it) */
  uint8_t mode;       /**< sys_uart_mode_e: FRAMED (0), RAW (1), RAW_LINE (2) */
  uint32_t baud;      /**< SYS_UART_BAUD_MIN..MAX */
  uint8_t parity;     /**< sys_uart_parity_e: NONE (0), EVEN (1), ODD (2) */
  uint8_t stop_bits;  /**< 1 or 2 */
  sys_io_pin_t tx;    /**< TX pin, or SYS_IO_PIN_NONE */
  sys_io_pin_t rx;    /**< RX pin, or SYS_IO_PIN_NONE */
  sys_io_pin_t rts;   /**< Optional RTS pin, or SYS_IO_PIN_NONE */
  sys_io_pin_t cts;   /**< Optional CTS pin, or SYS_IO_PIN_NONE */
} esp32s3_uart_cfg_t;

_Static_assert(sizeof(esp32s3_uart_cfg_t) == 16, "esp32s3_uart_cfg_t size");

typedef struct {
  sys_uart_rx_cb_f cb;
  void* ctx;
  bool allow_send;
} esp32s3_uart_hook_t;

/** @brief One statically allocated UART engine slot in .bss (tasks created once, parked on REMOVE). */
typedef struct {
  bool in_use;
  bool rtos_inited;
  bool hw_installed;
  bool tx_running;
  bool rx_running;
  volatile bool stopping;

  uint8_t dev_id;
  uart_port_t uart;
  uint8_t mode;
  uint32_t baud;
  uint32_t init_baud;
  uint8_t parity;
  uint8_t stop_bits;

  // Reader ownership (at most one active reader: SUBSCRIBE, RX_HOOK, or RECEIVE/FLUSH/TRANSACT).
  uint8_t sub_id;
  uint8_t sub_gen;
  uint16_t sub_op;
  esp32s3_uart_hook_t hook;
  bool hook_notify_up;
  bool rx_reader_busy;
  volatile uint32_t rx_flush_seq;

  // Overflow throttling for UART_EV_OVERFLOW.
  uint16_t ovf_pending;
  bool ovf_sent_once;
  TickType_t last_ovf_tick;

  QueueHandle_t evt_queue;

  StaticSemaphore_t lock_buf;
  SemaphoreHandle_t lock;
  StaticSemaphore_t parked_buf;
  SemaphoreHandle_t parked_sem;

  StaticRingbuffer_t tx_rb_buf;
  uint8_t tx_rb_mem[CONFIG_ESP32S3_UART_TX_QUEUE];
  RingbufHandle_t tx_rb;

  StaticRingbuffer_t urgent_rb_buf;
  uint8_t urgent_rb_mem[ESP32S3_UART_URGENT_QUEUE];
  RingbufHandle_t urgent_rb;

  StaticRingbuffer_t rx_rb_buf;
  uint8_t rx_rb_mem[CONFIG_ESP32S3_UART_RX_QUEUE];
  RingbufHandle_t rx_rb;

  StackType_t rx_stack[CONFIG_ESP32S3_UART_RX_TASK_STACK];
  StaticTask_t rx_tcb;
  TaskHandle_t rx_task;

  StackType_t tx_stack[CONFIG_ESP32S3_UART_TX_TASK_STACK];
  StaticTask_t tx_tcb;
  TaskHandle_t tx_task;

  // Line / packet staging in .bss so task stacks stay small.
  uint8_t line_tx[1u + ESP32S3_UART_COBS_MAX + 1u];
  struct __attribute__((packed)) {
    sys_uart_rx_t hdr;
    uint8_t data[ESP32S3_UART_COBS_MAX];
  } rx_pkt;
  uint8_t rx_chunk[ESP32S3_UART_RX_CHUNK];
} esp32s3_uart_slot_t;

/** @brief Claims an idle slot for `dev_id` on `uart_num` (fails with ERR_UART_IN_USE / ERR_DEV_NO_MEM). */
MUST_USE err_h esp32s3_uart_slot_claim(uint8_t dev_id, uint8_t uart_num, esp32s3_uart_slot_t** out_slot);

/** @brief Installs the ESP-IDF UART driver for `slot` according to `cfg` and starts its RX/TX tasks. */
MUST_USE err_h esp32s3_uart_slot_open(esp32s3_uart_slot_t* slot, const esp32s3_uart_cfg_t* cfg);

/** @brief Stops slot tasks, emits hook down if armed, deletes the ESP-IDF driver, and frees the slot. */
void esp32s3_uart_slot_release(esp32s3_uart_slot_t* slot);

/** @brief Restores initial baudrate and flushes TX/RX queues in place. */
MUST_USE err_h esp32s3_uart_slot_reset(esp32s3_uart_slot_t* slot);

/** @brief Empties all queued items from a FreeRTOS NOSPLIT ring buffer. */
void esp32s3_uart_drain_ring(RingbufHandle_t rb);

/** @brief Advances RX flush sequence and flushes both hardware RX FIFO and `rx_rb` (slot->lock held). */
void esp32s3_uart_flush_rx_locked(esp32s3_uart_slot_t* slot);

/** @brief True if SUBSCRIBE, RX_HOOK, or an in-flight synchronous read owns RX (slot->lock held). */
bool esp32s3_uart_has_reader_locked(const esp32s3_uart_slot_t* slot, uint8_t* out_owner);

/** @brief Wakes the RX task immediately (e.g., to emit the initial OP_RX_HOOK up callback). */
void esp32s3_uart_notify_rx_wakeup(esp32s3_uart_slot_t* slot);

/** @brief Authorizes sender and enqueues `frame` (with FRAMED CRC16 if enabled) to urgent or normal TX ring. */
MUST_USE err_h esp32s3_uart_send(esp32s3_uart_slot_t* slot, uint8_t caller, cmd_in_t frame, uint16_t wait_ms,
                                 uint8_t flags, bool has_token, const void* token);

/** @brief Pops one queued RX message from `rx_rb` into `out` within `wait` ticks (NULL return on timeout). */
MUST_USE err_h esp32s3_uart_receive_one(esp32s3_uart_slot_t* slot, TickType_t wait, cmd_out_t* out);
