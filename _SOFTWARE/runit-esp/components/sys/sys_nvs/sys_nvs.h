#pragma once

#include <stdint.h>

#include "errors.h"
#include "nvs.h"
#include "sys_nvs_errors.h"

/**
 * @brief Makes a partition ready for nvs_open. One NVS can't use (no free pages, a new version) is erased, with all it
 * held. Boot, the first step: no other module initialises or erases NVS.
 * @param[in] partition The NVS partition's label
 * @return The error of the NVS init or erase
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

/** @brief A JSON file: its NVS namespace and what it holds */
typedef struct {
  nvs_handle_t nvs;           /**< The file's namespace */
  sys_nvs_json_info_t info;   /**< Length, CRC and chunks so far */
} sys_nvs_json_t;

/**
 * @brief Opens the file `name` (its NVS namespace, at most 15 characters) in `partition`, ready by sys_nvs_init.
 * Boot, one call per file.
 * @param[out] file      The file
 * @param[in]  partition The NVS partition's label
 * @param[in]  name      The file's namespace
 * @return The error of the NVS open
 */
MUST_USE err_h sys_nvs_json_open(sys_nvs_json_t* file, const char* partition, const char* name);

/**
 * @brief Adds one chunk at the end. A power cut leaves the file as it was before the chunk or with it whole.
 * @param[in,out] file   The file
 * @param[in]     offset Must be the file's length: a chunk sent twice is refused
 * @param[in]     data   The chunk
 * @param[in]     len    Chunk size in bytes
 * @return ERR_NVS_OFFSET, ERR_NVS_BAD_ARG: no data, or the file has every chunk it can have
 */
MUST_USE err_h sys_nvs_json_append(sys_nvs_json_t* file, uint32_t offset, const uint8_t* data, uint32_t len);

/**
 * @brief Empties the file.
 * @param[in,out] file The file
 * @return The error of the NVS erase
 */
MUST_USE err_h sys_nvs_json_erase(sys_nvs_json_t* file);

/**
 * @brief Reads chunk `chunk` into `buf`.
 * @param[in]  file  The file
 * @param[in]  chunk Chunk index, 0..chunks-1
 * @param[out] buf   Buffer for the chunk
 * @param[in]  cap   Size of buf
 * @param[out] len   The chunk's bytes
 * @return ERR_NVS_NO_CHUNK: past the last one
 */
MUST_USE err_h sys_nvs_json_get(sys_nvs_json_t* file, uint16_t chunk, uint8_t* buf, uint32_t cap, uint32_t* len);

/**
 * @brief The file's length, CRC and chunks.
 * @param[in] file The file
 * @return Length, CRC and chunks
 */
sys_nvs_json_info_t sys_nvs_json_info(sys_nvs_json_t* file);
