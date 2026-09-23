#include "runit.h"
#include <esp_log.h>
#include "runit_board_cfg.h"
#include "runit_decoders.h"
#include "runit_error_policy.h"
#include "sys_actions.h"
#include "sys_data_connector.h"
#include "sys_device.h"
#include "sys_error_log.h"
#include "sys_event.h"
#include "sys_interface.h"
#include "sys_settings.h"
#include "vm_exec.h"
#include "vm_retain.h"
#include "vm_sub.h"

static const char* TAG = "runit_app";

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

err_h runit_start(void) {
  SE_init();

  static const runit_boot_step_entry_t s_boot_setup_steps[] = {
      {"runit_error_wiring_init", runit_error_wiring_init},
      {"runit_board_i2c_init", runit_board_i2c_init},
      {"sys_settings_init", sys_settings_init},
      {"runit_board_ble_init", runit_board_ble_init},
      {"sys_data_connector_init", sys_data_connector_init},
      {"runit_board_connector_bindings_init", runit_board_connector_bindings_init},
      {"runit_board_error_sink_init", runit_board_error_sink_init},
      {"SE_set_logging", runit_step_error_configure},
      {"sys_event_init", sys_event_init},
      {"runit_board_power_init", runit_board_power_init},
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
  };

  err = runit_run_boot_steps(s_boot_runtime_steps, sizeof(s_boot_runtime_steps) / sizeof(s_boot_runtime_steps[0]));
  if (err != NULL) {
    return err;
  }

  return NULL;
}
