#include "sys_h_bridge.h"
#include "esp_log.h"

static const char* TAG = "sys_h_bridge";

#undef OWNER
#define OWNER OWNER_SYS_IO_REGISTER_DRIVER // Let's use standard register owner or generic error owner

status_rep_t sys_h_bridge_register(uint8_t device_id, void* handle, const sys_h_bridge_contract_t* contract) {
  CHECK_NOT_NULL_RP(contract);

  sys_device_t *dev = sys_device_get_by_id(device_id);
  if (!dev) return STA_C(ERR_DEVICE_INSTALL_FAILED, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);

  dev->device_handle = handle;
  dev->contracts[SYS_DEVICE_CONTRACT_H_BRIDGE] = (void*)contract;

  ESP_LOGI(TAG, "H-bridge contract registered for device_id: %u", device_id);
  return STA_OK;
}

status_rep_t sys_h_bridge_unregister(uint8_t device_id) {
  sys_device_t *dev = sys_device_get_by_id(device_id);
  if (dev) {
    dev->contracts[SYS_DEVICE_CONTRACT_H_BRIDGE] = NULL;
  }
  return STA_OK;
}

status_rep_t sys_h_bridge_forward(uint8_t device_id, sys_h_bridge_mode_e mode, uint16_t duty, uint8_t h_id) {
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_H_BRIDGE, sys_h_bridge_contract_t, forward, mode, duty, h_id);
}

status_rep_t sys_h_bridge_backwards(uint8_t device_id, sys_h_bridge_mode_e mode, uint16_t duty, uint8_t h_id) {
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_H_BRIDGE, sys_h_bridge_contract_t, backwards, mode, duty, h_id);
}

status_rep_t sys_h_bridge_brake(uint8_t device_id, uint16_t duty, uint8_t h_id) {
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_H_BRIDGE, sys_h_bridge_contract_t, brake, duty, h_id);
}

status_rep_t sys_h_bridge_coast(uint8_t device_id, uint16_t duty, uint8_t h_id) {
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_H_BRIDGE, sys_h_bridge_contract_t, coast, duty, h_id);
}
