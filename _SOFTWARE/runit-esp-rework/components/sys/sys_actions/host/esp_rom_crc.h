#pragma once
/* Host stand-in for ESP-IDF's esp_rom_crc.h: the reflected CRC-32 of zlib / IEEE (crc 0 starts a new one). */
#include <stdint.h>

uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* buf, uint32_t len);
