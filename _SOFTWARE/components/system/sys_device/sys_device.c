#include "sys_device.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "sys_io.h"
#include "utils.h"


static const char* TAG = __FILE_NAME__;

static sys_device_t* s_device_registry[MAX_DEVICE_ID + 1] = {NULL};

R_MUTEX_DEFINE(sys_device_mutex);

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_INSTALL
status_rep_t sys_device_install(sys_device_t* device) {
  SYS_DEV_CHECK_NOT_NULL_R(device);
  SYS_DEV_CHECK_NOT_NULL_R(device->install_device);
  CHECK_ARG_R(device->device_id, 0, MAX_DEVICE_ID, device->device_id);

  R_MUTEX_LOCK(sys_device_mutex, WAIT_FOREVER);

  // Sprawdzenie, czy to ID nie jest już zajęte
  if (s_device_registry[device->device_id] != NULL) {
    R_MUTEX_UNLOCK(sys_device_mutex);
    return STA_C(ERR_ALREADY_EXISTS, OWNER, device->device_id, STATUS_PAYLOAD_UNKNOWN);
  }

  // Alokacja kopii struktury na stercie (tak jak w oryginalnym węźle)
  sys_device_t* new_dev = (sys_device_t*)calloc(1, sizeof(sys_device_t));
  if (!new_dev) {
    R_MUTEX_UNLOCK(sys_device_mutex);
    return STA_C(ERR_NO_MEM, OWNER, device->device_id, STATUS_PAYLOAD_UNKNOWN);
  }

  *new_dev = *device;
  memset(new_dev->contracts, 0, sizeof(new_dev->contracts));
  s_device_registry[device->device_id] = new_dev;  // Zapis do tablicy O(1)

  ESP_LOGI(TAG, "Installing device: %s (ID: %lu)", new_dev->name, new_dev->device_id);

  // Wywołanie zgodne z nowym kontraktem przyjmującym void**
  new_dev->device_handle = new_dev->install_device(new_dev->install_args);

  if (new_dev->device_handle == NULL) {
    ESP_LOGE(TAG, "Failed to install %s", new_dev->name);
    s_device_registry[device->device_id] = NULL;
    free(new_dev);
    R_MUTEX_UNLOCK(sys_device_mutex);
    return STA_C(ERR_DEVICE_INSTALL_FAILED, OWNER, device->device_id, STATUS_PAYLOAD_UNKNOWN);
  }

  R_MUTEX_UNLOCK(sys_device_mutex);
  return STA_OK;
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
status_rep_t sys_device_reset(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);

  if (!dev) return STA_C(ERR_NOT_FOUND, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
  if (!dev->reset_device) return STA_C(ERR_NOT_SUPPORTED, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);

  ESP_LOGW(TAG, "Resetting device: %s", dev->name);

  return dev->reset_device(dev->device_handle);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_UNINSTALL
status_rep_t sys_device_uninstall(uint8_t device_id) {
  CHECK_ARG_R(device_id, 0, MAX_DEVICE_ID, device_id);

  R_MUTEX_LOCK(sys_device_mutex, WAIT_FOREVER);

  sys_device_t* dev = s_device_registry[device_id];

  if (dev != NULL) {
    ESP_LOGW(TAG, "Deleting device: %s", dev->name);

    if (dev->uninstall_device) {
      dev->uninstall_device(dev->device_handle);
    }

    free(dev);
    s_device_registry[device_id] = NULL;  // Zwolnienie indeksu
  }

  R_MUTEX_UNLOCK(sys_device_mutex);
  return STA_OK;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SUSPEND
status_rep_t sys_device_suspend(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) return STA_C(ERR_NOT_FOUND, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
  if (!dev->suspend_device) return STA_C(ERR_NOT_SUPPORTED, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
  ESP_LOGI(TAG, "Suspending device: %s", dev->name);
  return dev->suspend_device(dev->device_handle);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_RESUME
status_rep_t sys_device_resume(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) return STA_C(ERR_NOT_FOUND, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
  if (!dev->resume_device) return STA_C(ERR_NOT_SUPPORTED, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
  ESP_LOGI(TAG, "Resuming device: %s", dev->name);
  return dev->resume_device(dev->device_handle);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SUSPEND_ALL
status_rep_t sys_device_suspend_all(void) {
  status_rep_t status = STA_OK;
  for (int i = 0; i <= MAX_DEVICE_ID; i++) {
    sys_device_t* dev = sys_device_get_by_id(i);
    if (dev && dev->suspend_device) {
      status_rep_t ret = dev->suspend_device(dev->device_handle);
      if (STA_IS_ERR(ret)) {
        status = ret;
        ESP_LOGE(TAG, "Failed to suspend device: %s", dev->name);
      }
    }
  }
  return status;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_RESUME_ALL
status_rep_t sys_device_resume_all(void) {
  status_rep_t status = STA_OK;
  for (int i = 0; i <= MAX_DEVICE_ID; i++) {
    sys_device_t* dev = sys_device_get_by_id(i);
    if (dev && dev->resume_device) {
      status_rep_t ret = dev->resume_device(dev->device_handle);
      if (STA_IS_ERR(ret)) {
        status = ret;
        ESP_LOGE(TAG, "Failed to resume device: %s", dev->name);
      }
    }
  }
  return status;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_FREEZE
status_rep_t sys_device_freeze(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) return STA_C(ERR_NOT_FOUND, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
  if (!dev->freeze_device) return STA_C(ERR_NOT_SUPPORTED, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
  ESP_LOGD(TAG, "Freezing device: %s", dev->name);
  return dev->freeze_device(dev->device_handle);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SYNC
status_rep_t sys_device_sync(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) return STA_C(ERR_NOT_FOUND, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
  if (!dev->sync_device) return STA_C(ERR_NOT_SUPPORTED, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
  ESP_LOGD(TAG, "Syncing device: %s", dev->name);
  return dev->sync_device(dev->device_handle);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_FREEZE_ALL
status_rep_t sys_device_freeze_all(void) {
  status_rep_t status = STA_OK;
  for (int i = 0; i <= MAX_DEVICE_ID; i++) {
    sys_device_t* dev = sys_device_get_by_id(i);
    if (dev && dev->freeze_device) {
      status_rep_t ret = dev->freeze_device(dev->device_handle);
      if (STA_IS_ERR(ret)) {
        status = ret;
        ESP_LOGE(TAG, "Failed to freeze device: %s", dev->name);
      }
    }
  }
  return status;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SYNC_ALL
status_rep_t sys_device_sync_all(void) {
  status_rep_t status = STA_OK;
  for (int i = 0; i <= MAX_DEVICE_ID; i++) {
    sys_device_t* dev = sys_device_get_by_id(i);
    if (dev && dev->sync_device) {
      status_rep_t ret = dev->sync_device(dev->device_handle);
      if (STA_IS_ERR(ret)) {
        status = ret;
        ESP_LOGE(TAG, "Failed to sync device: %s", dev->name);
      }
    }
  }
  return status;
}

void* sys_device_allocate_ctx(size_t total_size, void** install_args) {
  if (total_size < sizeof(sys_device_adapter_base_t)) return NULL;
  sys_device_adapter_base_t* base = calloc(1, total_size);
  if (!base) return NULL;

  base->device_id = (uint8_t)(uintptr_t)install_args[0];
  base->is_frozen = false;

  return base;
}
