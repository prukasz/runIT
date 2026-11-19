#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What a UART line carries, shared by the ports (device_esp32s3_uart) and the console (provider_console):
 *  - a FRAMED line: 00 cobs([frame][crc16 LE]) 00, CRC16-CCITT (poly 0x1021, init 0xFFFF, no reflection, no final
 *    xor); a RAW line: the bytes as they are;
 *  - the TX queue: a send copies the frame in (and its CRC on a FRAMED line), urgent frames go first, a SPARE one takes
 *    only half of the queue; the writer's task takes the next one as it goes on the line.
 */

#define SYS_UART_CRC_BYTES     2u
#define SYS_UART_COBS_MAX(len) ((len) + ((len) / 254u) + 2u)
#define SYS_UART_WIRE_MAX      SYS_UART_COBS_MAX(CMD_FRAME_MAX + SYS_UART_CRC_BYTES)  // a frame between its 0x00s

/**
 * @brief One candidate of a FRAMED line (the bytes between two 0x00), decoded in place, its CRC checked.
 * @return The frame's bytes at `buf`; 0 for anything else (boot text, noise, a frame cut by an overflow).
 */
uint32_t sys_uart_unframe(uint8_t* buf, uint32_t len);

/** @brief A TX queue: two rings on the heap, the urgent one written first. */
typedef struct {
  RingbufHandle_t rb[2];  /**< [normal, urgent] */
  uint32_t cap[2];
  TaskHandle_t task;      /**< The writer, woken by a push; set before the first push */
  bool framed;
  uint8_t line[1u + SYS_UART_WIRE_MAX + 1u];  /**< What sys_uart_txq_pop() gives the writer */
} sys_uart_txq_t;

/** @brief Makes the rings: the normal one `bytes` (a multiple of 4), the urgent one two whole frames. @return false
 *  when the heap has no room (free it with sys_uart_txq_free). */
bool sys_uart_txq_init(sys_uart_txq_t* q, uint32_t bytes, bool framed);

/** @brief Frees the rings; nobody pushes or pops any more. */
void sys_uart_txq_free(sys_uart_txq_t* q);

/** @brief Drops everything queued. */
void sys_uart_txq_clear(sys_uart_txq_t* q);

/**
 * @brief Queues one frame, 1..CMD_FRAME_MAX bytes, and wakes the writer.
 * @param[in] flags CMD_SEND_URGENT (the urgent ring), CMD_SEND_SPARE (refused past half of its ring), CMD_SEND_NOWAIT
 * @param[in] wait  How long to wait for room without NOWAIT
 * @return false: no room
 */
bool sys_uart_txq_push(sys_uart_txq_t* q, cmd_in_t frame, uint8_t flags, TickType_t wait);

/**
 * @brief The writer's: the next frame as it goes on the line, in `q->line` (urgent ones first); sleeps until a push
 * or a notification of the writer's task, at most `wait`.
 * @return Bytes in `q->line`; 0 when none came.
 */
uint32_t sys_uart_txq_pop(sys_uart_txq_t* q, TickType_t wait);

#ifdef __cplusplus
}
#endif
