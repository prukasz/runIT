#include "sys_uart_codec.h"

uint16_t sys_uart_crc16(uint16_t crc, const uint8_t* data, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

uint32_t sys_uart_cobs_encode(const uint8_t* in, uint32_t len, uint8_t* out) {
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

uint32_t sys_uart_cobs_decode(uint8_t* buf, uint32_t len) {
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
