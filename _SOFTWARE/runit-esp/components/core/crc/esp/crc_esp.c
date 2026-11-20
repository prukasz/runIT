#include "crc.h"

#include "esp_rom_crc.h"

uint32_t crc32_ieee(uint32_t crc, const void* data, uint32_t len) {
  return esp_rom_crc32_le(crc, (const uint8_t*)data, len);
}
