#pragma once

/**
 * @file nvs_store.h
 * @brief The interface of the board's non-volatile store (NVS_STORE.MD): named values (blobs) in namespaces of a
 * partition, as one struct of functions (nvs_store_t) with no type of the implementation in it. The build has one of
 * them, `nvs_store`, which the implementation defines: this chip's is esp/ (ESP-IDF's nvs_flash). What is built on
 * the interface (a JSON file of chunks) is nvs_store_json.h.
 * @usage nvs_store_ns_t ns;
 *        ERR_TRY(nvs_store.open("nvs", "board_id", &ns));
 *        ERR_TRY(nvs_store.set(ns, "id", &id, sizeof(id)));
 */

#include <stdint.h>

#include "errors.h"
#include "nvs_store_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * The store: one struct of functions, the implementation's (esp/)
 * ------------------------------------------------------------------------- */

/** @brief An open namespace. */
typedef uint32_t nvs_store_ns_t;

/** @brief A key of a namespace, for `keys`: on the caller's task, `key` valid for the call only. */
typedef void (*nvs_store_key_f)(const char* key, void* ctx);

/** @brief The store's interface. Keys and namespaces are at most 15 characters. */
typedef struct {
  /**
   * @brief Makes a partition ready. One the store can't use (no free pages, a new version) is erased, with all it
   * held. Boot, the first step: nothing else initialises or erases a partition.
   * @return NULL; the implementation's error
   */
  MUST_USE err_h (*init)(const char* partition);

  /**
   * @brief Opens namespace `name` of `partition`, made ready by `init`. Boot, once per namespace: it stays open.
   * @return NULL; the implementation's error
   */
  MUST_USE err_h (*open)(const char* partition, const char* name, nvs_store_ns_t* ns);

  /**
   * @brief Reads value `key`: its size into `len` (in: the room in `buf`), and with `buf` the value; `buf` NULL: the
   * size only.
   * @return NULL; ERR_NVS_NOT_FOUND, the implementation's error (a value longer than `len`)
   */
  MUST_USE err_h (*get)(nvs_store_ns_t ns, const char* key, void* buf, uint32_t* len);

  /**
   * @brief Writes value `key` and commits it: after the call it survives a power cut.
   * @return NULL; the implementation's error
   */
  MUST_USE err_h (*set)(nvs_store_ns_t ns, const char* key, const void* data, uint32_t len);

  /**
   * @brief Erases value `key`, or with `key` NULL every value of the namespace, and commits.
   * @return NULL; ERR_NVS_NOT_FOUND (a key that is not there), the implementation's error
   */
  MUST_USE err_h (*erase)(nvs_store_ns_t ns, const char* key);

  /**
   * @brief Calls `fn` with each key of the namespace. `fn` never writes or erases in it.
   * @return NULL; the implementation's error
   */
  MUST_USE err_h (*keys)(nvs_store_ns_t ns, nvs_store_key_f fn, void* ctx);
} nvs_store_t;

/** @brief The store of this build: defined by its implementation (esp/nvs_store_esp.c). */
extern const nvs_store_t nvs_store;

#ifdef __cplusplus
}
#endif
