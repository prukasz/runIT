#pragma once

/**
 * @file nvs_store_json.h
 * @brief A JSON file of chunks, built on the store's interface (nvs_store.h) and nothing else (NVS_STORE.MD "JSON
 * files"): bytes kept in chunks, one namespace of its own. The content is not parsed.
 * @usage ERR_TRY(nvs_store_json_open(&file, "nvs", "board"));
 */

#include <stdint.h>

#include "errors.h"
#include "nvs_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a JSON file holds; the app compares `bytes` and `crc32` with what it sent. */
typedef struct __attribute__((packed)) {
  uint32_t bytes;   /**< Length so far */
  uint32_t crc32;   /**< CRC-32 of all the chunks (zlib / IEEE) */
  uint16_t chunks;  /**< Chunks stored: 0..chunks-1 can be read */
} nvs_store_json_info_t;

/** @brief A JSON file: its namespace and what it holds */
typedef struct {
  nvs_store_ns_t ns;           /**< The file's namespace */
  nvs_store_json_info_t info;  /**< Length, CRC and chunks so far */
} nvs_store_json_t;

/**
 * @brief Opens the file `name` (its namespace) in `partition`, made ready by nvs_store.init. Boot, one call per file.
 * @param[out] file      The file
 * @param[in]  partition The partition's label
 * @param[in]  name      The file's namespace
 * @return NULL; the error of the open
 */
MUST_USE err_h nvs_store_json_open(nvs_store_json_t* file, const char* partition, const char* name);

/**
 * @brief Adds one chunk at the end. A power cut leaves the file as it was before the chunk or with it whole.
 * @param[in,out] file   The file
 * @param[in]     offset Must be the file's length: a chunk sent twice is refused
 * @param[in]     data   The chunk
 * @param[in]     len    Chunk size in bytes
 * @return ERR_NVS_OFFSET, ERR_NVS_BAD_ARG: no data, or the file has every chunk it can have
 */
MUST_USE err_h nvs_store_json_append(nvs_store_json_t* file, uint32_t offset, const uint8_t* data, uint32_t len);

/**
 * @brief Empties the file.
 * @param[in,out] file The file
 * @return The error of the erase
 */
MUST_USE err_h nvs_store_json_erase(nvs_store_json_t* file);

/**
 * @brief Reads chunk `chunk` into `buf`.
 * @param[in]  file  The file
 * @param[in]  chunk Chunk index, 0..chunks-1
 * @param[out] buf   Buffer for the chunk
 * @param[in]  cap   Size of buf
 * @param[out] len   The chunk's bytes
 * @return ERR_NVS_NO_CHUNK: past the last one
 */
MUST_USE err_h nvs_store_json_get(nvs_store_json_t* file, uint16_t chunk, uint8_t* buf, uint32_t cap, uint32_t* len);

/**
 * @brief The file's length, CRC and chunks.
 * @param[in] file The file
 * @return Length, CRC and chunks
 */
nvs_store_json_info_t nvs_store_json_info(nvs_store_json_t* file);

#ifdef __cplusplus
}
#endif
