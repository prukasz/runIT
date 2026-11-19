#pragma once

/**
 * @file nvs_store_iface.h
 * @brief The interface an implementation of the store fills (NVS_STORE.MD "Implementations"): one struct of
 * functions, and the build's one instance of it, `nvs_store_impl`, which the implementation defines
 * (esp/nvs_store_esp.c). Included by an implementation and by nvs_store.c only; the modules use nvs_store.h.
 * @usage const nvs_store_iface_t nvs_store_impl = {.init = my_init, .open = my_open, ...};
 */

#include <stdint.h>

#include "errors.h"
#include "nvs_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What an implementation does: each function as its nvs_store_* (nvs_store.h); "not found" is
 * ERR_NVS_NOT_FOUND. */
typedef struct {
  MUST_USE err_h (*init)(const char* partition);
  MUST_USE err_h (*open)(const char* partition, const char* name, nvs_store_ns_t* ns);
  MUST_USE err_h (*get)(nvs_store_ns_t ns, const char* key, void* buf, uint32_t* len);
  MUST_USE err_h (*set)(nvs_store_ns_t ns, const char* key, const void* data, uint32_t len);
  MUST_USE err_h (*erase)(nvs_store_ns_t ns, const char* key);
  MUST_USE err_h (*keys)(nvs_store_ns_t ns, nvs_store_key_f fn, void* ctx);
} nvs_store_iface_t;

/** @brief The build's implementation: defined by the one it builds. */
extern const nvs_store_iface_t nvs_store_impl;

#ifdef __cplusplus
}
#endif
