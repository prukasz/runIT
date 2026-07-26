#include "sys_device.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "sys_error.h"
#include "sys_io.h"
#include "utils.h"

static const char* TAG = __FILE_NAME__;

/*Registry mutations (install / uninstall) are init/config context only.
  Reads are lock-free: sys_device_get_by_id() sits on the hot dispatch path and
  is reachable from ISR-adjacent code, where a mutex cannot be taken.*/
static sys_device_t* s_device_registry[MAX_DEVICE_ID + 1] = {NULL};

#define DEV_OP(d, f) ((d)->cls->ops.f)
#define DEV_NAME(d) ((d)->cls->name)

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_INSTALL
err_h sys_device_install_cfg(const sys_device_class_t* cls, uint8_t device_id, const void* cfg, size_t cfg_size) {
  SE_CHECK_NOT_NULL(cls);
  SE_CHECK_NOT_NULL(cls->ops.install);
  SE_CHECK_IN_RANGE(device_id, 0, MAX_DEVICE_ID);

  if (s_device_registry[device_id] != NULL) {
    SE_RET_ERR(ERR_DEV_ALREADY_EXIST, device_id);
  }

  sys_device_t* new_dev = (sys_device_t*)calloc(1, sizeof(sys_device_t));
  SE_CHECK_IF_ALLOCATED(new_dev);

  /*Heap-copy the config: the caller's struct is typically a stack compound
    literal that dies as soon as create() returns.*/
  if (cfg != NULL && cfg_size > 0) {
    new_dev->cfg = malloc(cfg_size);
    if (new_dev->cfg == NULL) {
      free(new_dev);
      SE_RET_ERR(ERR_BASE_NO_MEM, 0);
    }
    memcpy(new_dev->cfg, cfg, cfg_size);
    new_dev->cfg_size = cfg_size;
  }

  new_dev->device_id = device_id;
  new_dev->cls = cls;

  /*cls is set before install: adapters (and their dependencies) may look this
    device up mid-install and read dev->cls->contracts[]. The state stays
    INSTALLING until install succeeds, so SYS_DEV_DISPATCH still refuses it in
    the meantime.*/
  new_dev->state = SYS_DEV_STATE_INSTALLING;
  s_device_registry[device_id] = new_dev;

  ESP_LOGI(TAG, "Installing device: %s (ID: %u)", cls->name, device_id);

  err_h install_status = cls->ops.install(new_dev->cfg, &new_dev->device_handle);

  if (SE_IS_ERR(install_status)) {
    ESP_LOGE(TAG, "Failed to install %s (ID: %u)", cls->name, device_id);
    s_device_registry[device_id] = NULL;
    free(new_dev->cfg);
    free(new_dev);
    SE_RET_IF_ERR(install_status);
  }

  new_dev->state = SYS_DEV_STATE_INSTALLED;

  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_GET_BY_ID
sys_device_t* sys_device_get_by_id(uint8_t device_id) {
  if (device_id > MAX_DEVICE_ID) return NULL;
  sys_device_t* found_device = s_device_registry[device_id];
  return found_device;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_RESET
err_h sys_device_reset(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);

  if (!dev) SE_RET_ERR(ERR_DEV_NOT_FOUND, device_id);
  if (!SYS_DEV_IS_INSTALLED(dev)) SE_RET_ERR(ERR_DEV_NOT_INSTALLED, device_id);
  if (SYS_DEV_IS_SUSPENDED(dev)) SE_RET_ERR(ERR_DEV_SUSPENDED, device_id);

  err_h (*fn)(void*) = DEV_OP(dev, reset);
  if (!fn) SE_RET_ERR(ERR_BASE_NOT_SUPPORTED, 0);

  ESP_LOGW(TAG, "Resetting device: %s", DEV_NAME(dev));

  err_h err = fn(dev->device_handle);
  SE_RET_IF_ERR(err);
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_UNINSTALL
err_h sys_device_uninstall(uint8_t device_id) {
  SE_CHECK_IN_RANGE(device_id, 0, MAX_DEVICE_ID);

  sys_device_t* dev = s_device_registry[device_id];

  if (dev != NULL) {
    dev->state = SYS_DEV_STATE_NONE;
    s_device_registry[device_id] = NULL;  // Zwolnienie indeksu

    err_h (*fn)(void*) = DEV_OP(dev, uninstall);
    if (fn) {
      fn(dev->device_handle);
    }
    ESP_LOGW(TAG, "Deleted device: %s", DEV_NAME(dev));
    /*Released only after the adapter has torn down - it may still be reading
      its own copy of the config until then.*/
    free(dev->cfg);
    free(dev);
    return NULL;
  }
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SUSPEND
err_h sys_device_suspend(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) SE_RET_ERR(ERR_DEV_NOT_FOUND, device_id);
  if (!SYS_DEV_IS_INSTALLED(dev)) SE_RET_ERR(ERR_DEV_NOT_INSTALLED, device_id);
  if (SYS_DEV_IS_SUSPENDED(dev)) return NULL;

  err_h (*fn)(void*) = DEV_OP(dev, suspend);
  if (!fn) SE_RET_ERR(ERR_BASE_NOT_SUPPORTED, 0);
  ESP_LOGI(TAG, "Suspending device: %s", DEV_NAME(dev));
  err_h err = fn(dev->device_handle);
  SE_RET_IF_ERR(err);
  dev->state = SYS_DEV_STATE_SUSPENDED;
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_RESUME
err_h sys_device_resume(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) SE_RET_ERR(ERR_DEV_NOT_FOUND, device_id);
  if (!SYS_DEV_IS_INSTALLED(dev)) SE_RET_ERR(ERR_DEV_NOT_INSTALLED, device_id);
  if (!SYS_DEV_IS_SUSPENDED(dev)) return NULL;

  err_h (*fn)(void*) = DEV_OP(dev, resume);
  if (!fn) SE_RET_ERR(ERR_BASE_NOT_SUPPORTED, 0);
  ESP_LOGI(TAG, "Resuming device: %s", DEV_NAME(dev));
  err_h err = fn(dev->device_handle);
  SE_RET_IF_ERR(err);
  dev->state = SYS_DEV_STATE_INSTALLED;
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SUSPEND_ALL
err_h sys_device_suspend_all(void) {
  for (int i = 0; i <= MAX_DEVICE_ID; i++) {
    sys_device_t* dev = sys_device_get_by_id(i);
    if (!dev || !SYS_DEV_IS_READY(dev)) continue;
    err_h (*fn)(void*) = DEV_OP(dev, suspend);
    if (fn) {
      err_h ret = fn(dev->device_handle);
      if (SE_IS_ERR(ret)) {
        ESP_LOGE(TAG, "Failed to suspend device: %s", DEV_NAME(dev));
        SE_RET_IF_ERR(ret);
      }
      dev->state = SYS_DEV_STATE_SUSPENDED;
    }
  }
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_RESUME_ALL
err_h sys_device_resume_all(void) {
  for (int i = 0; i <= MAX_DEVICE_ID; i++) {
    sys_device_t* dev = sys_device_get_by_id(i);
    if (!dev || !SYS_DEV_IS_SUSPENDED(dev)) continue;
    err_h (*fn)(void*) = DEV_OP(dev, resume);
    if (fn) {
      err_h ret = fn(dev->device_handle);
      if (SE_IS_ERR(ret)) {
        ESP_LOGE(TAG, "Failed to resume device: %s", DEV_NAME(dev));
        SE_RET_IF_ERR(ret);
      }
      dev->state = SYS_DEV_STATE_INSTALLED;
    }
  }
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_FREEZE
err_h sys_device_freeze(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) SE_RET_ERR(ERR_DEV_NOT_FOUND, device_id);
  if (!SYS_DEV_IS_INSTALLED(dev)) SE_RET_ERR(ERR_DEV_NOT_INSTALLED, device_id);
  if (SYS_DEV_IS_SUSPENDED(dev)) SE_RET_ERR(ERR_DEV_SUSPENDED, device_id);

  err_h (*fn)(void*) = DEV_OP(dev, freeze);
  if (!fn) SE_RET_ERR(ERR_BASE_NOT_SUPPORTED, 0);
  ESP_LOGD(TAG, "Freezing device: %s", DEV_NAME(dev));
  err_h err = fn(dev->device_handle);
  SE_RET_IF_ERR(err);
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SYNC
err_h sys_device_sync(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) SE_RET_ERR(ERR_DEV_NOT_FOUND, device_id);
  if (!SYS_DEV_IS_INSTALLED(dev)) SE_RET_ERR(ERR_DEV_NOT_INSTALLED, device_id);
  if (SYS_DEV_IS_SUSPENDED(dev)) SE_RET_ERR(ERR_DEV_SUSPENDED, device_id);

  err_h (*fn)(void*) = DEV_OP(dev, sync);
  if (!fn) SE_RET_ERR(ERR_BASE_NOT_SUPPORTED, 0);
  ESP_LOGD(TAG, "Syncing device: %s", DEV_NAME(dev));
  err_h err = fn(dev->device_handle);
  SE_RET_IF_ERR(err);
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_FREEZE_ALL
err_h sys_device_freeze_all(void) {
  for (int i = 0; i <= MAX_DEVICE_ID; i++) {
    sys_device_t* dev = sys_device_get_by_id(i);
    if (!dev || !SYS_DEV_IS_READY(dev)) continue;
    err_h (*fn)(void*) = DEV_OP(dev, freeze);
    if (fn) {
      err_h ret = fn(dev->device_handle);
      if (SE_IS_ERR(ret)) {
        ESP_LOGE(TAG, "Failed to freeze device: %s", DEV_NAME(dev));
        SE_RET_IF_ERR(ret);
      }
    }
  }
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SYNC_ALL
err_h sys_device_sync_all(void) {
  for (int i = 0; i <= MAX_DEVICE_ID; i++) {
    sys_device_t* dev = sys_device_get_by_id(i);
    if (!dev || !SYS_DEV_IS_READY(dev)) continue;
    err_h (*fn)(void*) = DEV_OP(dev, sync);
    if (fn) {
      err_h ret = fn(dev->device_handle);
      if (SE_IS_ERR(ret)) {
        ESP_LOGE(TAG, "Failed to sync device: %s", DEV_NAME(dev));
        SE_RET_IF_ERR(ret);
      }
    }
  }
  return NULL;
}


