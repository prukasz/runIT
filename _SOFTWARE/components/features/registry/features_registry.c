#include "feature_registry.h"
#include <stdlib.h>
#include <string.h>
#include "utils.h"

#define TAG "FEAT_REG"
#define OWNER OWNER_FEATURES_REGISTRY

static feature_node_t* s_features_head = NULL;
R_MUTEX_DEFINE(s_features_mutex);

err_h feature_alloc(uint8_t id, size_t bytes, feature_teardown_fn teardown, void** out_handle) {
  SE_CHECK_NOT_NULL(out_handle);
  *out_handle = NULL;

  if (bytes == 0) {
    SE_FAIL(ERR_INVALID_VAL_UI32, .val = 0, .min = 1, .max = UINT32_MAX);
  }

  if (R_MUTEX_LOCK(s_features_mutex, portMAX_DELAY) != pdTRUE) {
    SE_FAIL(ERR_ESP_ERR, .esp_code = ESP_ERR_TIMEOUT);
  }

  /* Check for duplicate ID */
  feature_node_t* curr = NULL;
  LL_FOREACH(s_features_head, curr) {
    if (curr->id == id) {
      R_MUTEX_UNLOCK(s_features_mutex);
      SE_FAIL(ERR_BASE_INVALID_STATE, 0);
    }
  }

  /* Allocate node */
  feature_node_t* node = (feature_node_t*)malloc(sizeof(feature_node_t));
  if (!node) {
    R_MUTEX_UNLOCK(s_features_mutex);
    SE_FAIL(ERR_BASE_NO_MEM, 0);
  }

  /* Allocate payload data */
  void* data = malloc(bytes);
  if (!data) {
    free(node);
    R_MUTEX_UNLOCK(s_features_mutex);
    SE_FAIL(ERR_BASE_NO_MEM, 0);
  }

  memset(data, 0, bytes);
  node->id = id;
  node->size = bytes;
  node->data = data;
  node->teardown = teardown;
  node->next = NULL;

  LL_APPEND(s_features_head, node);

  R_MUTEX_UNLOCK(s_features_mutex);

  *out_handle = data;
  return NULL;
}

void* feature_get_by_id(uint8_t id) {
  void* result = NULL;

  if (R_MUTEX_LOCK(s_features_mutex, portMAX_DELAY) != pdTRUE) {
    return NULL;
  }

  feature_node_t* curr = NULL;
  LL_FOREACH(s_features_head, curr) {
    if (curr->id == id) {
      result = curr->data;
      break;
    }
  }

  R_MUTEX_UNLOCK(s_features_mutex);
  return result;
}

err_h feature_remove(uint8_t id) {
  if (R_MUTEX_LOCK(s_features_mutex, portMAX_DELAY) != pdTRUE) {
    SE_FAIL(ERR_ESP_ERR, .esp_code = ESP_ERR_TIMEOUT);
  }

  feature_node_t* target = NULL;
  feature_node_t* curr = NULL;
  LL_FOREACH(s_features_head, curr) {
    if (curr->id == id) {
      target = curr;
      break;
    }
  }

  if (!target) {
    R_MUTEX_UNLOCK(s_features_mutex);
    SE_FAIL(ERR_BASE_NOT_FOUND, 0);
  }

  LL_DELETE(s_features_head, target);
  R_MUTEX_UNLOCK(s_features_mutex);

  /* Invoke optional teardown before freeing memory */
  if (target->teardown && target->data) {
    target->teardown(target->data);
  }

  if (target->data) {
    free(target->data);
  }
  free(target);

  return NULL;
}

err_h feature_remove_all(void) {
  if (R_MUTEX_LOCK(s_features_mutex, portMAX_DELAY) != pdTRUE) {
    SE_FAIL(ERR_ESP_ERR, .esp_code = ESP_ERR_TIMEOUT);
  }

  feature_node_t* curr = NULL;
  feature_node_t* tmp = NULL;

  LL_FOREACH_SAFE(s_features_head, curr, tmp) {
    LL_DELETE(s_features_head, curr);

    if (curr->teardown && curr->data) {
      curr->teardown(curr->data);
    }
    if (curr->data) {
      free(curr->data);
    }
    free(curr);
  }

  s_features_head = NULL;
  R_MUTEX_UNLOCK(s_features_mutex);
  return NULL;
}

