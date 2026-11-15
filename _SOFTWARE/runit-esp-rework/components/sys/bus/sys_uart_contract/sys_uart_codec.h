#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Shared COBS + CRC16-CCITT framing codec for FRAMED UART ports and links:
 *        00 cobs([frame][crc16 LE]) 00 (poly 0x1021, init 0xFFFF, no reflection, no final xor).
 */

#define SYS_UART_CRC_BYTES     2u
#define SYS_UART_CRC16_INIT    0xFFFFu
#define SYS_UART_COBS_MAX(len) ((len) + ((len) / 254u) + 2u)

/** @brief CRC16-CCITT (poly 0x1021, init 0xFFFF) carried on from `crc` over `data[0..len)`. */
uint16_t sys_uart_crc16(uint16_t crc, const uint8_t* data, uint32_t len);

/** @brief COBS-encodes `in[0..len)` into `out` (capacity >= SYS_UART_COBS_MAX(len)); returns bytes written. */
uint32_t sys_uart_cobs_encode(const uint8_t* in, uint32_t len, uint8_t* out);

/** @brief COBS-decodes `buf[0..len)` in place; returns decoded bytes, or 0 when `buf` is invalid COBS. */
uint32_t sys_uart_cobs_decode(uint8_t* buf, uint32_t len);

#ifdef __cplusplus
}
#endif
