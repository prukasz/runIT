#pragma once

#include <stdint.h>

#include "errors.h"
#include "nvs.h"
#include "sys_nvs_errors.h"

/**
 * @brief Makes a partition ready for nvs_open. One NVS can't use (no free pages, a new version) is erased, with all it
 * held. Boot, the first step: no other module initialises or erases NVS.
 */
MUST_USE err_h sys_nvs_init(const char* partition);

/* ----------------------------------------------------------------------------
 * A JSON file: bytes kept in chunks, one namespace of its own. The content is not parsed.
 * ------------------------------------------------------------------------- */

/** @brief What a JSON file holds; the app compares `bytes` and `crc32` with what it sent. */
typedef struct __attribute__((packed)) {
  uint32_t bytes;   /**< Length so far */
  uint32_t crc32;   /**< esp_rom_crc32_le of all the chunks (zlib / IEEE) */
  uint16_t chunks;  /**< Chunks stored: 0..chunks-1 can be read */
} sys_nvs_json_info_t;

typedef struct {
  nvs_handle_t nvs;
  sys_nvs_json_info_t info;
} sys_nvs_json_t;

/**
 * @brief Opens the file `name` (its NVS namespace, at most 15 characters) in `partition`, ready by sys_nvs_init.
 * Boot, one call per file.
 */
MUST_USE err_h sys_nvs_json_open(sys_nvs_json_t* file, const char* partition, const char* name);

/**
 * @brief Adds one chunk at the end. `offset` must be the file's length (ERR_NVS_OFFSET): a chunk sent twice is refused.
 * ERR_NVS_BAD_ARG: no data, or the file has every chunk it can have. A power cut leaves the file as it was before the
 * chunk or with it whole.
 */
MUST_USE err_h sys_nvs_json_append(sys_nvs_json_t* file, uint32_t offset, const uint8_t* data, uint32_t len);

/** @brief Empties the file. */
MUST_USE err_h sys_nvs_json_erase(sys_nvs_json_t* file);

/** @brief Reads chunk `chunk` into `buf` (ERR_NVS_NO_CHUNK: past the last one). `len`: its bytes. */
MUST_USE err_h sys_nvs_json_get(sys_nvs_json_t* file, uint16_t chunk, uint8_t* buf, uint32_t cap, uint32_t* len);

/** @brief The file's length, CRC and chunks. */
sys_nvs_json_info_t sys_nvs_json_info(sys_nvs_json_t* file);
