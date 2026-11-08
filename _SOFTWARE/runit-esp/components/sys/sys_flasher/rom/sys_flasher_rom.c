#include "sys_flasher_rom.h"

#include <string.h>

#define DIR_COMMAND 0x00u
#define DIR_ANSWER  0x01u

static void put_u16(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t* p, uint32_t v) {
  put_u16(p, v);
  put_u16(p + 2, v >> 16);
}

static uint32_t get_u32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t sys_flasher_slip_encode(const uint8_t* in, uint32_t len, uint8_t* out) {
  uint32_t n = 0;
  out[n++] = SYS_FLASHER_SLIP_END;
  for (uint32_t i = 0; i < len; i++) {
    if (in[i] == SYS_FLASHER_SLIP_END) {
      out[n++] = SYS_FLASHER_SLIP_ESC;
      out[n++] = SYS_FLASHER_SLIP_ESC_END;
    } else if (in[i] == SYS_FLASHER_SLIP_ESC) {
      out[n++] = SYS_FLASHER_SLIP_ESC;
      out[n++] = SYS_FLASHER_SLIP_ESC_ESC;
    } else {
      out[n++] = in[i];
    }
  }
  out[n++] = SYS_FLASHER_SLIP_END;
  return n;
}

void sys_flasher_slip_start(sys_flasher_slip_rx_t* rx, uint8_t* buf, uint32_t cap) {
  *rx = (sys_flasher_slip_rx_t){.buf = buf, .cap = cap};
}

bool sys_flasher_slip_feed(sys_flasher_slip_rx_t* rx, uint8_t byte) {
  if (byte == SYS_FLASHER_SLIP_END) {
    // An END with bytes before it ends a packet; one with none starts one (two ENDs between packets).
    const bool whole = rx->in && rx->len > 0 && !rx->overflow;
    if (whole) {
      rx->in = false;
      return true;
    }
    rx->in = true;
    rx->len = 0;
    rx->esc = false;
    rx->overflow = false;
    return false;
  }
  if (!rx->in) {
    return false;  // text between packets
  }
  if (rx->esc) {
    rx->esc = false;
    byte = byte == SYS_FLASHER_SLIP_ESC_END ? SYS_FLASHER_SLIP_END
           : byte == SYS_FLASHER_SLIP_ESC_ESC ? SYS_FLASHER_SLIP_ESC
                                              : byte;
  } else if (byte == SYS_FLASHER_SLIP_ESC) {
    rx->esc = true;
    return false;
  }
  if (rx->len < rx->cap) {
    rx->buf[rx->len++] = byte;
  } else {
    rx->overflow = true;
  }
  return false;
}

uint32_t sys_flasher_rom_checksum(const uint8_t* data, uint32_t len) {
  uint8_t sum = SYS_FLASHER_ROM_SEED;
  for (uint32_t i = 0; i < len; i++) {
    sum ^= data[i];
  }
  return sum;
}

uint32_t sys_flasher_rom_command(uint8_t op, const uint8_t* data, uint32_t len, uint32_t checksum, uint8_t* out) {
  out[0] = DIR_COMMAND;
  out[1] = op;
  put_u16(out + 2, len);
  put_u32(out + 4, checksum);
  if (len > 0) {
    memmove(out + SYS_FLASHER_ROM_HEAD, data, len);  // `data` may already sit there
  }
  return SYS_FLASHER_ROM_HEAD + len;
}

bool sys_flasher_rom_parse(const uint8_t* packet, uint32_t len, sys_flasher_rom_answer_t* out) {
  if (len < SYS_FLASHER_ROM_HEAD + SYS_FLASHER_ROM_STATUS || packet[0] != DIR_ANSWER) {
    return false;
  }
  const uint32_t size = (uint32_t)packet[2] | ((uint32_t)packet[3] << 8);
  if (size < SYS_FLASHER_ROM_STATUS || SYS_FLASHER_ROM_HEAD + size != len) {
    return false;
  }
  const uint8_t* status = packet + len - SYS_FLASHER_ROM_STATUS;
  *out = (sys_flasher_rom_answer_t){
      .op = packet[1],
      .value = get_u32(packet + 4),
      .data = packet + SYS_FLASHER_ROM_HEAD,
      .len = size - SYS_FLASHER_ROM_STATUS,
      .status = status[0],
      .error = status[1],
  };
  return true;
}

void sys_flasher_md5_hex(const uint8_t digest[16], char out[32]) {
  static const char k_hex[] = "0123456789abcdef";
  for (uint32_t i = 0; i < 16; i++) {
    out[2 * i] = k_hex[digest[i] >> 4];
    out[2 * i + 1] = k_hex[digest[i] & 0x0Fu];
  }
}
