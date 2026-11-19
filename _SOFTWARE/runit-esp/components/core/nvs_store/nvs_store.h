#pragma once

/**
 * @file nvs_store.h
 * @brief The board's non-volatile store (NVS_STORE.MD), the one every module keeps its values through: named values
 * (blobs) in namespaces of a partition, and a JSON file of chunks built on them. No type of an implementation here:
 * each call goes to the build's implementation through its interface (nvs_store_iface.h); this chip's is esp/
 * (ESP-IDF's nvs_flash).
 * @usage nvs_store_ns_t ns;
 *        ERR_TRY(nvs_store_open("nvs", "board_id", &ns));
 *        ERR_TRY(nvs_store_set(ns, "id", &id, sizeof(id)));
 */

#include <stdint.h>

#include "errors.h"
#include "nvs_store_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * The store. Keys and namespaces are at most 15 characters.
 * ------------------------------------------------------------------------- */

/** @brief An open namespace. */
typedef uint32_t nvs_store_ns_t;

/** @brief A key of a namespace, for nvs_store_keys: on the caller's task, `key` valid for the call only. */
typedef void (*nvs_store_key_f)(const char* key, void* ctx);

/**
 * @brief Makes a partition ready. One the store can't use (no free pages, a new version) is erased, with all it held.
 * Boot, the first step: nothing else initialises or erases a partition.
 * @param[in] partition The partition's label
 * @return NULL; the implementation's error
 */
MUST_USE err_h nvs_store_init(const char* partition);

/**
 * @brief Opens namespace `name` of `partition`, made ready by nvs_store_init. Boot, once per namespace: it stays open.
 * @param[in]  partition The partition's label
 * @param[in]  name      The namespace
 * @param[out] ns        The namespace
 * @return NULL; the implementation's error
 */
MUST_USE err_h nvs_store_open(const char* partition, const char* name, nvs_store_ns_t* ns);

/**
 * @brief Reads value `key`: its size into `len`, and with `buf` the value too.
 * @param[in]     ns  The namespace
 * @param[in]     key The key
 * @param[out]    buf The value, or NULL for its size only
 * @param[in,out] len In: the room in `buf`; out: the value's size
 * @return NULL; ERR_NVS_NOT_FOUND, the implementation's error (a value longer than `len`)
 */
MUST_USE err_h nvs_store_get(nvs_store_ns_t ns, const char* key, void* buf, uint32_t* len);

/**
 * @brief Writes value `key` and commits it: after the call it survives a power cut.
 * @param[in] ns   The namespace
 * @param[in] key  The key
 * @param[in] data The value
 * @param[in] len  Its size
 * @return NULL; the implementation's error
 */
MUST_USE err_h nvs_store_set(nvs_store_ns_t ns, const char* key, const void* data, uint32_t len);

/**
 * @brief Erases value `key`, or with `key` NULL every value of the namespace, and commits.
 * @param[in] ns  The namespace
 * @param[in] key The key, or NULL
 * @return NULL; ERR_NVS_NOT_FOUND (a key that is not there), the implementation's error
 */
MUST_USE err_h nvs_store_erase(nvs_store_ns_t ns, const char* key);

/**
 * @brief Calls `fn` with each key of the namespace. `fn` never writes or erases in it.
 * @param[in] ns  The namespace
 * @param[in] fn  Called per key
 * @param[in] ctx Given to `fn`
 * @return NULL; the implementation's error
 */
MUST_USE err_h nvs_store_keys(nvs_store_ns_t ns, nvs_store_key_f fn, void* ctx);

/* ----------------------------------------------------------------------------
 * A JSON file: bytes kept in chunks, one namespace of its own. The content is not parsed.
 * ------------------------------------------------------------------------- */

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
 * @brief Opens the file `name` (its namespace) in `partition`, made ready by nvs_store_init. Boot, one call per file.
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
