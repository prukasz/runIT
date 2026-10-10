// The decoder headers' DBG() calls keep firing on the sys_interface switch
// (components/utils/Kconfig), as they did when sys_interface included them.
// Must precede the includes below.
#define DBG_ENABLE CONFIG_DBG_ENABLE_SYS_INTERFACE

#include "runit.h"
#include <esp_log.h>
#include <esp_system.h>
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
#include "runit_board_cfg.h"
#include "runit_error_policy.h"
#include "sys_actions.h"
#include "sys_data_connector.h"
#include "sys_device.h"
#include "sys_error_log.h"
#include "sys_event.h"
#include "sys_interface.h"
#include "sys_project.h"
#include "sys_settings.h"
#include "utils.h"
#include "vm_loader.h"
#include "vm_exec.h"
#include "vm_retain.h"
#include "vm_sub.h"

static const char* TAG = "runit_app";

/* Registers every inbound packet class with sys_interface. Boot step; runs before sys_interface_init()
   starts the RX receiver. */
static SE_MUST_USE err_h runit_register_decoders(void) {
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

/* Replays the stored code (sys_project) and starts the VM when autostart is set. Runtime boot step, after
   vm_exec_start(). Never aborts the boot: a replay problem is reported and the board runs with what applied. */
static SE_MUST_USE err_h runit_project_boot(void) {
  SE_REPORT(sys_project_replay(dec_settings_project_is_vm_frame));
  sys_project_report_t report;
  bool autostart = false;
  sys_project_report(&report, &autostart);
  if (autostart && report.state == SYS_PROJECT_REPLAY_DONE && !report.vm_failed && vm_loader_state() == VM_LOAD_OPEN) {
    SE_REPORT(vm_exec_control(VM_EXEC_NORMAL_MODE));
  }
  return NULL;
}

void runit_enter_safe_state(void) {
  err_h stop = vm_exec_stop();
  if (!stop) vm_retain_save_stopped();  // keep retained values: power may be about to go
  SE_release(stop);
  SE_release(sys_device_suspend_all());
  ESP_LOGE(TAG, "System entered safe state (VM stopped, ready devices suspended)");
}

err_h runit_run_boot_steps(const runit_boot_step_entry_t* steps, size_t count) {
  if (steps == NULL || count == 0) return NULL;
  for (size_t i = 0; i < count; i++) {
    const runit_boot_step_entry_t* step = &steps[i];
    if (step->fn == NULL) continue;
    err_h err = step->fn();
    if (err != NULL) {
      ESP_LOGE(TAG, "runIT boot aborted at step: %s", step->name ? step->name : "unnamed");
      SE_release(SE_send(err));
      runit_enter_safe_state();
      return err;
    }
  }
  return NULL;
}

static SE_MUST_USE err_h runit_step_error_configure(void) {
  return SE_set_logging(ESP_LOG_INFO, true, true);
}

/* Why the chip started. Needed on the native USB console: the ROM's own "rst:" line
   is gone before the host reopens the re-enumerated port. */
static const char* reset_reason_name(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "USB";
    default: return "other";
  }
}

err_h runit_start(void) {
  SE_init();
  esp_reset_reason_t reason = esp_reset_reason();
  ESP_LOGI(TAG, "reset reason: %s (%d)", reset_reason_name(reason), (int)reason);

  static const runit_boot_step_entry_t s_boot_setup_steps[] = {
      {"runit_error_wiring_init", runit_error_wiring_init},
      {"runit_board_i2c_init", runit_board_i2c_init},
      {"sys_settings_init", sys_settings_init},
      {"sys_project_init", sys_project_init},
      {"runit_board_ble_init", runit_board_ble_init},
      {"sys_data_connector_init", sys_data_connector_init},
      {"runit_board_connector_bindings_init", runit_board_connector_bindings_init},
      {"runit_board_error_sink_init", runit_board_error_sink_init},
      {"SE_set_logging", runit_step_error_configure},
      {"sys_event_init", sys_event_init},
      {"runit_board_power_init", runit_board_power_init},
      {"devices_register_classes", devices_register_classes},
      {"runit_register_decoders", runit_register_decoders},
      {"sys_interface_init", sys_interface_init},
      {"runit_board_bind_boot_action", runit_board_bind_boot_action},
      {"sys_actions_init", sys_actions_init},
      {"runit_board_invoke_boot_action", runit_board_invoke_boot_action},
      {"vm_sub_init", vm_sub_init},
      {"vm_retain_init", vm_retain_init},
  };

  err_h err = runit_run_boot_steps(s_boot_setup_steps, sizeof(s_boot_setup_steps) / sizeof(s_boot_setup_steps[0]));
  if (err != NULL) {
    return err;
  }

  ESP_LOGI(TAG, "runIT boot sequence complete");

  static const runit_boot_step_entry_t s_boot_runtime_steps[] = {
      {"vm_exec_start", vm_exec_start},
      {"runit_project_boot", runit_project_boot},
  };

  err = runit_run_boot_steps(s_boot_runtime_steps, sizeof(s_boot_runtime_steps) / sizeof(s_boot_runtime_steps[0]));
  if (err != NULL) {
    return err;
  }

  return NULL;
}
