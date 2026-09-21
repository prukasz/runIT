#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "sys_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*feature_teardown_fn)(void* data);

typedef struct feature_node_t {
  uint8_t id;
  size_t size;
  void* data;
  feature_teardown_fn teardown;
  struct feature_node_t* next;
} feature_node_t;

/**
 * @brief Allocates and registers a new feature instance by ID.
 *
 * @param id Unique feature identifier.
 * @param bytes Number of bytes to allocate for the feature structure.
 * @param teardown Optional teardown function to invoke before freeing on removal (can be NULL).
 * @param out_handle Pointer receiving the allocated and zero-initialized feature data.
 * @return err_h NULL on success, or sys_error handle.
 */
err_h feature_alloc(uint8_t id, size_t bytes, feature_teardown_fn teardown, void** out_handle);

/**
 * @brief Looks up a registered feature's data by ID.
 *
 * @param id Feature ID.
 * @return void* Pointer to the feature data, or NULL if not found.
 */
void* feature_get_by_id(uint8_t id);

/**
 * @brief Removes and frees a single feature by ID.
 *
 * @param id Feature ID to remove.
 * @return err_h NULL on success, or sys_error handle.
 */
err_h feature_remove(uint8_t id);

/**
 * @brief Removes and frees all registered features.
 *
 * @return err_h NULL on success.
 */
err_h feature_remove_all(void);

#ifdef __cplusplus
}
#endif

