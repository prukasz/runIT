#include <string.h>

#include "sys_ble_priv.h"

bool sys_ble_frag_send(const uint8_t* data, size_t len, size_t room, uint8_t* pkt, sys_ble_frag_tx_f tx, void* ctx) {
  const size_t chunk = room - 1;  // the header byte
  size_t off = 0;
  uint8_t count = 0;
  do {
    const size_t n = len - off < chunk ? len - off : chunk;
    pkt[0] = (uint8_t)((off == 0 ? SYS_BLE_FRAG_FIRST : 0) | (off + n == len ? SYS_BLE_FRAG_LAST : 0) |
                       (count & SYS_BLE_FRAG_COUNT_MASK));
    memcpy(&pkt[1], data + off, n);
    if (!tx(ctx, pkt, n + 1)) {
      return false;
    }
    off += n;
    count++;
  } while (off < len);
  return true;
}

void sys_ble_frag_rx_reset(sys_ble_frag_rx_t* rx) {
  rx->active = false;
}

size_t sys_ble_frag_rx(sys_ble_frag_rx_t* rx, uint16_t uuid, const uint8_t* data, size_t len) {
  if (len == 0) {
    return 0;
  }
  const uint8_t hdr = data[0];
  if (hdr & SYS_BLE_FRAG_FIRST) {
    rx->len = 0;
    rx->expected = 0;
    rx->active = true;
  }
  if (!rx->active || (hdr & SYS_BLE_FRAG_COUNT_MASK) != rx->expected) {
    sys_ble_report(ERR_NEW(ERR_BLE_FRAG, .uuid = uuid, .hdr = hdr, .expected = rx->expected));
    rx->active = false;  // the rest of this frame is dropped up to the next FIRST
    return 0;
  }
  const size_t n = len - 1;
  if (rx->len + n > sizeof(rx->buf)) {
    sys_ble_report(ERR_NEW(ERR_BLE_TOO_LONG, .uuid = uuid, .len = (uint16_t)(rx->len + n), .max = sizeof(rx->buf)));
    rx->active = false;
    return 0;
  }
  memcpy(rx->buf + rx->len, data + 1, n);
  rx->len += n;
  rx->expected = (uint8_t)((rx->expected + 1) & SYS_BLE_FRAG_COUNT_MASK);
  if (!(hdr & SYS_BLE_FRAG_LAST)) {
    return 0;
  }
  rx->active = false;
  return rx->len;
}
