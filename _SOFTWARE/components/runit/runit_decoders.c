// The decoder headers' DBG() calls keep firing on the sys_interface switch
// (components/utils/Kconfig), as they did when sys_interface included them.
// Must precede the includes below.
#define DBG_ENABLE CONFIG_DBG_ENABLE_SYS_INTERFACE

#include "runit_decoders.h"
#include <sdkconfig.h>
#include "dec_features.h"
#include "dec_settings_ble.h"
#include "dec_settings_data_connector.h"
#include "dec_settings_logs.h"
#include "dec_settings_power.h"
#include "dec_settings_project.h"
#include "dec_sys_actions.h"
#include "dec_sys_contracts.h"
#include "dec_sys_events.h"
#include "dec_vm_loader.h"
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

err_h runit_register_decoders(void) {
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SYS_CONTRACTS, dec_sys_contracts_decode, "sys_contracts"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SETTINGS_BLE, dec_settings_ble_decode, "settings_ble"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SETTINGS_DATA_CONNECTOR, dec_settings_data_connector_decode, "settings_data_connector"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SETTINGS_LOGS, dec_settings_logs_decode, "settings_logs"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SETTINGS_POWER, dec_settings_power_decode, "settings_power"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_VM_LOADER, dec_vm_loader_decode, "vm_loader"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SYS_FEATURES, dec_features_decode, "features"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SYS_ACTIONS, dec_sys_actions_decode, "sys_actions"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SYS_EVENTS, dec_sys_events_decode, "sys_events"));
  SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SETTINGS_PROJECT, dec_settings_project_decode, "settings_project"));
  return NULL;
}
