// The decoder headers' DBG() calls keep firing on the sys_interface switch
// (components/utils/Kconfig), as they did when sys_interface included them.
// Must precede the includes below.
#define DBG_ENABLE CONFIG_DBG_ENABLE_SYS_INTERFACE

#include "runit_decoders.h"
#include <sdkconfig.h>
#include "dec_settings_ble.h"
#include "dec_settings_data_connector.h"
#include "dec_settings_logs.h"
#include "dec_settings_power.h"
#include "dec_settings_project.h"
#include "dec_sys_actions.h"
#include "dec_sys_contracts.h"
#include "dec_sys_events.h"
#include "dec_vm_loader.h"
#include "devices.h"
#include "sys_interface.h"
#include "utils.h"

err_h runit_project_boot(void) {
  SE_REPORT(sys_project_replay(dec_settings_project_is_vm_frame));
  sys_project_report_t report;
  bool autostart = false;
  sys_project_report(&report, &autostart);
  if (autostart && report.state == SYS_PROJECT_REPLAY_DONE && !report.vm_failed && vm_loader_state() == VM_LOAD_OPEN) {
    SE_REPORT(vm_exec_control(VM_EXEC_NORMAL_MODE));
  }
  return NULL;
}

// Device types for the packet router's create frame (0x00, type_id, cfg): every install
// packet of dec_sys_device_install.h, keyed by its header byte. Same decoders as the
// 0x40..0x47 install packets, so both entry points build the device the same way.
#define DEVICE_TYPE_CREATE_FN(header, packet_type, decoder_func)                 \
  static err_h create_type_##header(const void* cfg) {                          \
    packet_type packet;                                                         \
    memcpy(&packet, cfg, sizeof(packet));                                       \
    return decoder_func(&packet);                                               \
  }
SYS_CONTRACTS_INSTALL_PACKET_LIST(DEVICE_TYPE_CREATE_FN)

#define DEVICE_TYPE_REGISTER(header, packet_type, decoder_func) \
  SE_TRY(sys_device_register_type((header), sizeof(packet_type), create_type_##header));

// Devices that take the wire cfg directly: no decoder, the create frame's bytes are the cfg.
SYS_DEVICE_TYPE_FN(d_pca9685_cfg_t, d_pca9685_create)
SYS_DEVICE_TYPE_FN(d_servo_cfg_t, d_servo_create)

static SE_MUST_USE err_h register_device_types(void) {
  SYS_CONTRACTS_INSTALL_PACKET_LIST(DEVICE_TYPE_REGISTER)
  SE_TRY(sys_device_register_type(PCA9685_TYPE_ID, sizeof(d_pca9685_cfg_t), type_create_d_pca9685_create));
  SE_TRY(sys_device_register_type(SERVO_TYPE_ID, sizeof(d_servo_cfg_t), type_create_d_servo_create));
  return NULL;
}

err_h runit_register_decoders(void) {
  SE_TRY(register_device_types());
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SYS_CONTRACTS, dec_sys_contracts_decode, "sys_contracts"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SETTINGS_BLE, dec_settings_ble_decode, "settings_ble"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SETTINGS_DATA_CONNECTOR, dec_settings_data_connector_decode, "settings_data_connector"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SETTINGS_LOGS, dec_settings_logs_decode, "settings_logs"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SETTINGS_POWER, dec_settings_power_decode, "settings_power"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_VM_LOADER, dec_vm_loader_decode, "vm_loader"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SYS_ACTIONS, dec_sys_actions_decode, "sys_actions"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SYS_EVENTS, dec_sys_events_decode, "sys_events"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SETTINGS_PROJECT, dec_settings_project_decode, "settings_project"));
  return NULL;
}
