#pragma once
#include "status.h"
#include "sys_device.h"
#include "sys_power.h" // for sys_h_bridge_mode_e

typedef struct sys_h_bridge_contract_t {
  status_rep_t (*forward)(void* device_handle, sys_h_bridge_mode_e mode, uint16_t duty, uint8_t h_id);
  status_rep_t (*backwards)(void* device_handle, sys_h_bridge_mode_e mode, uint16_t duty, uint8_t h_id);
  status_rep_t (*brake)(void* device_handle, uint16_t duty, uint8_t h_id);
  status_rep_t (*coast)(void* device_handle, uint16_t duty, uint8_t h_id);
} sys_h_bridge_contract_t;

status_rep_t sys_h_bridge_register(uint8_t device_id, void* handle, const sys_h_bridge_contract_t* contract);
status_rep_t sys_h_bridge_unregister(uint8_t device_id);

status_rep_t sys_h_bridge_forward(uint8_t device_id, sys_h_bridge_mode_e mode, uint16_t duty, uint8_t h_id);
status_rep_t sys_h_bridge_backwards(uint8_t device_id, sys_h_bridge_mode_e mode, uint16_t duty, uint8_t h_id);
status_rep_t sys_h_bridge_brake(uint8_t device_id, uint16_t duty, uint8_t h_id);
status_rep_t sys_h_bridge_coast(uint8_t device_id, uint16_t duty, uint8_t h_id);
