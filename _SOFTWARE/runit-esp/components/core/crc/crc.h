#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file crc.h
 * @brief The board's CRCs (CRC.MD): the calls, with the chip's implementation behind them (esp/: the ROM's).
 */

/**
 * @brief CRC-32 (zlib / IEEE, reflected 0xEDB88320) of `len` bytes, going on from `crc`.
 * @param crc  0 to start one, else the CRC of the bytes before.
 * @param data The bytes.
 * @param len  How many.
 * @return The CRC of everything so far.
 */
uint32_t crc32_ieee(uint32_t crc, const void* data, uint32_t len);

#ifdef __cplusplus
}
#endif
