#include "sys_uart_codec.h"

#include <string.h>

#define ITEM_BYTES(n) ((((uint32_t)(n) + 3u) & ~3u) + 8u)  // a NOSPLIT ring's room for an item of n bytes

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

uint32_t sys_uart_unframe(uint8_t* buf, uint32_t len) {
  const uint32_t n = cobs_decode(buf, len);
  if (n <= SYS_UART_CRC_BYTES || n - SYS_UART_CRC_BYTES > CMD_FRAME_MAX) {
    return 0;
  }
  const uint32_t m = n - SYS_UART_CRC_BYTES;
  return crc16(buf, m) == (uint16_t)(buf[m] | (buf[m + 1u] << 8)) ? m : 0;
}

/* ----------------------------------------------------------------------------
 * The TX queue
 * ------------------------------------------------------------------------- */

// Bytes taken in a NOSPLIT ring. xRingbufferGetCurFreeSize() can't tell: it caps the free size at the largest item.
static uint32_t ring_used(RingbufHandle_t rb, uint32_t cap) {
  UBaseType_t free_off = 0, acq_off = 0, items = 0;
  vRingbufferGetInfo(rb, &free_off, NULL, NULL, &acq_off, &items);
  if (acq_off != free_off) {
    return acq_off > free_off ? (uint32_t)(acq_off - free_off) : cap - (uint32_t)(free_off - acq_off);
  }
  return items > 0 ? cap : 0u;
}

bool sys_uart_txq_init(sys_uart_txq_t* q, uint32_t bytes, bool framed) {
  *q = (sys_uart_txq_t){.cap = {bytes, 2u * ITEM_BYTES(CMD_FRAME_MAX + SYS_UART_CRC_BYTES)}, .framed = framed};
  for (uint32_t u = 0; u < 2; u++) {
    q->rb[u] = xRingbufferCreate(q->cap[u], RINGBUF_TYPE_NOSPLIT);
  }
  return q->rb[0] != NULL && q->rb[1] != NULL;
}

void sys_uart_txq_free(sys_uart_txq_t* q) {
  for (uint32_t u = 0; u < 2; u++) {
    if (q->rb[u] != NULL) {
      vRingbufferDelete(q->rb[u]);
      q->rb[u] = NULL;
    }
  }
}

void sys_uart_txq_clear(sys_uart_txq_t* q) {
  for (uint32_t u = 0; u < 2; u++) {
    size_t len = 0;
    void* item = NULL;
    while ((item = xRingbufferReceive(q->rb[u], &len, 0)) != NULL) {
      vRingbufferReturnItem(q->rb[u], item);
    }
  }
}

// SPARE: a snapshot before the room is taken, so two senders at once may overshoot half by an item.
bool sys_uart_txq_push(sys_uart_txq_t* q, cmd_in_t frame, uint8_t flags, TickType_t wait) {
  const uint32_t u = (flags & CMD_SEND_URGENT) ? 1u : 0u;
  const uint32_t len = frame.len + (q->framed ? SYS_UART_CRC_BYTES : 0u);
  if ((flags & CMD_SEND_SPARE) && ring_used(q->rb[u], q->cap[u]) + ITEM_BYTES(len) > q->cap[u] / 2u) {
    return false;
  }
  uint8_t* item = NULL;
  if (xRingbufferSendAcquire(q->rb[u], (void**)&item, len, (flags & CMD_SEND_NOWAIT) ? 0 : wait) != pdTRUE) {
    return false;
  }
  memcpy(item, frame.data, frame.len);
  if (q->framed) {
    const uint16_t crc = crc16(frame.data, frame.len);
    item[frame.len] = (uint8_t)crc;
    item[frame.len + 1u] = (uint8_t)(crc >> 8);
  }
  xRingbufferSendComplete(q->rb[u], item);
  if (q->task != NULL) {
    xTaskNotifyGive(q->task);
  }
  return true;
}

/* A push between the empty rings and the wait leaves its notification pending: the wait returns at once. */
uint32_t sys_uart_txq_pop(sys_uart_txq_t* q, TickType_t wait) {
  for (uint32_t round = 0; round < 2; round++) {
    for (int u = 1; u >= 0; u--) {
      size_t len = 0;
      uint8_t* item = xRingbufferReceive(q->rb[u], &len, 0);
      if (item == NULL) {
        continue;
      }
      uint32_t n = (uint32_t)len;
      if (q->framed) {  // [0x00][cobs][0x00]: a text line before the frame ends at the first 0x00
        q->line[0] = 0;
        n = cobs_encode(item, (uint32_t)len, q->line + 1) + 2u;
        q->line[n - 1u] = 0;
      } else {
        memcpy(q->line, item, len);
      }
      vRingbufferReturnItem(q->rb[u], item);
      return n;
    }
    if (round == 0) {
      (void)ulTaskNotifyTake(pdTRUE, wait);
    }
  }
  return 0;
}
