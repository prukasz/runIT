#include "sys_device.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "status_codes.h"
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
    return STA_C(ERR_DEV_ALREADY_EXISTS, OWNER, DEV_ERR_PACK(device->device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
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

  status_rep_t install_status = new_dev->install_device(new_dev->install_args, &new_dev->device_handle);

  if (STA_IS_ERR(install_status)) {
    ESP_LOGE(TAG, "Failed to install %s id(%u)", new_dev->name, new_dev->device_id);

    s_device_registry[device->device_id] = NULL;
    free(new_dev);
    R_MUTEX_UNLOCK(sys_device_mutex);

    STA_P(install_status);
    return install_status;
  }

  new_dev->is_installed = true;
  new_dev->is_suspended = false;

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

  if (!dev) return STA_C(ERR_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (!dev->is_installed) return STA_C(ERR_DEV_NOT_INSTALLED, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (dev->is_suspended) return STA_C(ERR_DEV_SUSPENDED, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
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
    dev->is_installed = false;

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
  if (!dev) return STA_C(ERR_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (!dev->is_installed) return STA_C(ERR_DEV_NOT_INSTALLED, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (dev->is_suspended) return STA_OK;
  if (!dev->suspend_device) return STA_C(ERR_NOT_SUPPORTED, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
  ESP_LOGI(TAG, "Suspending device: %s", dev->name);
  status_rep_t stat = dev->suspend_device(dev->device_handle);
  if (STA_IS_OK(stat)) {
    dev->is_suspended = true;
  }
  return stat;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_RESUME
status_rep_t sys_device_resume(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) return STA_C(ERR_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (!dev->is_installed) return STA_C(ERR_DEV_NOT_INSTALLED, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (!dev->is_suspended) return STA_OK;
  if (!dev->resume_device) return STA_C(ERR_NOT_SUPPORTED, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
  ESP_LOGI(TAG, "Resuming device: %s", dev->name);
  status_rep_t stat = dev->resume_device(dev->device_handle);
  if (STA_IS_OK(stat)) {
    dev->is_suspended = false;
  }
  return stat;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SUSPEND_ALL
status_rep_t sys_device_suspend_all(void) {
  status_rep_t status = STA_OK;
  for (int i = 0; i <= MAX_DEVICE_ID; i++) {
    sys_device_t* dev = sys_device_get_by_id(i);
    if (dev && dev->is_installed && !dev->is_suspended && dev->suspend_device) {
      status_rep_t ret = dev->suspend_device(dev->device_handle);
      if (STA_IS_ERR(ret)) {
        status = ret;
        ESP_LOGE(TAG, "Failed to suspend device: %s", dev->name);
      } else {
        dev->is_suspended = true;
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
    if (dev && dev->is_installed && dev->is_suspended && dev->resume_device) {
      status_rep_t ret = dev->resume_device(dev->device_handle);
      if (STA_IS_ERR(ret)) {
        status = ret;
        ESP_LOGE(TAG, "Failed to resume device: %s", dev->name);
      } else {
        dev->is_suspended = false;
      }
    }
  }
  return status;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_FREEZE
status_rep_t sys_device_freeze(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) return STA_C(ERR_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (!dev->is_installed) return STA_C(ERR_DEV_NOT_INSTALLED, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (dev->is_suspended) return STA_C(ERR_DEV_SUSPENDED, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (!dev->freeze_device) return STA_C(ERR_NOT_SUPPORTED, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
  ESP_LOGD(TAG, "Freezing device: %s", dev->name);
  return dev->freeze_device(dev->device_handle);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SYNC
status_rep_t sys_device_sync(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) return STA_C(ERR_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (!dev->is_installed) return STA_C(ERR_DEV_NOT_INSTALLED, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (dev->is_suspended) return STA_C(ERR_DEV_SUSPENDED, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
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
    if (dev && dev->is_installed && !dev->is_suspended && dev->freeze_device) {
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
    if (dev && dev->is_installed && !dev->is_suspended && dev->sync_device) {
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

status_rep_t sys_device_generic_error_handler(sys_device_t* dev, status_rep_t* err) {
  if (!dev || !err) return STA_OK;

  uint32_t e_code = err->e_code;

  if (e_code == ERR_DEV_SUSPENDED) {
    ESP_LOGI("SYS_DEV", "Generic handler: Automatically resuming suspended device '%s' (ID: %u)", dev->name, dev->device_id);
    sys_device_resume(dev->device_id);
    return STA_OK;
  }

  if (e_code == ERR_DEV_NOT_FOUND || e_code == ERR_DEV_NOT_INSTALLED) {
    ESP_LOGW("SYS_DEV", "Generic handler: Device '%s' (ID: %u) is not active/installed. Error: %s",
             dev->name, dev->device_id, status_error_to_name(e_code));
    return STA_OK;
  }

  ESP_LOGW("SYS_DEV", "Generic handler: Device '%s' (ID: %u) unhandled error %s (0x%04lx), severity: %d, payload: %llu",
           dev->name, dev->device_id, status_error_to_name(e_code), (unsigned long)e_code, err->details.severity, (unsigned long long)err->payload);

  if (err->details.severity == STATUS_CRITICAL) {
    if (dev->is_installed && !dev->is_suspended) {
      ESP_LOGE("SYS_DEV", "Critical error! Resetting device '%s' (ID: %u)", dev->name, dev->device_id);
      sys_device_reset(dev->device_id);
    } else {
      ESP_LOGW("SYS_DEV", "Cannot reset device '%s' (ID: %u) because it is suspended or not installed.", dev->name, dev->device_id);
    }
  }
  return STA_OK;
}
