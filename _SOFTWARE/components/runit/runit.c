#include "runit.h"
#include <esp_log.h>
#include <string.h>
#include "runit_board_cfg.h"
#include "runit_board_defs.h"
#include "runit_board_devices.h"
#include "sys_actions.h"
#include "sys_actions_static.h"
#include "sys_callbacks.h"
#include "sys_device.h"
#include "sys_error_config.h"
#include "sys_interface.h"
#include "vm_bench.h"
#include "vm_selftest.h"
#include "vm_sub.h"
#include "vm_exec.h"

static const char* TAG = "runit_app";

#if RUNIT_SKIP_DEVICE_INIT
/* Stands in for runit_at_boot so action 1 still resolves -- see the switch's
   comment in runit_board_cfg.h for why it is bound rather than skipped. */
static err_h runit_at_boot_disabled(void) {
  ESP_LOGW(TAG, "device init SKIPPED (RUNIT_SKIP_DEVICE_INIT)");
  return NULL;
}
#endif

static const sys_error_cfg_t s_runit_error_cfg = {
    .errors =
        {
            .tx_header = PACKET_HEADER_ERRORS,
            .packet_max = SE_ERR_PACKET_MAX,
        },
};

void runit_enter_safe_state(void) {
  vm_exec_stop();
  vm_exec_set_sample_hook(NULL);
  (void)sys_device_freeze_all();
  ESP_LOGE(TAG, "System entered safe state (VM stopped, ready devices frozen)");
}

err_h runit_run_boot_steps(const runit_boot_step_entry_t* steps, size_t count) {
  if (steps == NULL || count == 0) return NULL;
  for (size_t i = 0; i < count; i++) {
    const runit_boot_step_entry_t* step = &steps[i];
    if (step->fn == NULL) continue;
    err_h err = step->fn();
    if (err != NULL) {
      ESP_LOGE(TAG, "runIT boot aborted at step: %s", step->name ? step->name : "unnamed");
      SE_push_to_handler(err);
      runit_enter_safe_state();
      return err;
    }
  }
  return NULL;
}

#include "runit_device_error_policy.h"

static err_h runit_step_error_configure(void) {
  SE_set_logging(ESP_LOG_INFO, true, true);
  return SE_configure(&s_runit_error_cfg);
}

static err_h step_bind_boot_action(void) {
#if RUNIT_SKIP_DEVICE_INIT
  return sys_actions_bind_static(SYS_ACTION_ID_BOOT, runit_at_boot_disabled);
#else
  return sys_actions_bind_static(SYS_ACTION_ID_BOOT, runit_at_boot);
#endif
}

err_h runit_start(void) {
  SE_init();

  static const runit_boot_step_entry_t s_boot_setup_steps[] = {
      {"sys_start_i2c", sys_start_i2c},
      {"sys_power_static_config", sys_power_static_config},
      {"sys_ble_static_config", sys_ble_static_config},
      {"sys_data_connector_init", runit_data_connector_static_config},
      {"SE_configure", runit_step_error_configure},
      {"sys_callbacks_init", sys_callbacks_init},
      {"sys_interface_init", sys_interface_init},
      {"sys_actions_bind_boot", step_bind_boot_action},
      {"sys_actions_init", sys_actions_init},
      {"vm_sub_init", vm_sub_init},
  };

  err_h err = runit_run_boot_steps(s_boot_setup_steps, sizeof(s_boot_setup_steps) / sizeof(s_boot_setup_steps[0]));
  if (err != NULL) {
    return err;
  }

  ESP_LOGI(TAG, "runIT boot sequence complete");

#if RUNIT_ENABLE_VM_SELFTEST
  /* After sys_interface_init() so class 0x04 is registered -- the test
     injects real frames through sys_interface_decode() rather than calling
     the loader directly. */
  vm_selftest_run();
#endif
#if RUNIT_ENABLE_VM_BENCH
  vm_bench_run();
#endif

  // Tests and benchmarks own synchronous execution during boot. Restore
  // production defaults and start the supervisor stopped before accepting remote
  // execution-control packets.
  vm_exec_stop();
  vm_exec_set_sample_hook(NULL);

  static const runit_boot_step_entry_t s_boot_runtime_steps[] = {
      {"vm_exec_start", vm_exec_start},
  };

  err = runit_run_boot_steps(s_boot_runtime_steps, sizeof(s_boot_runtime_steps) / sizeof(s_boot_runtime_steps[0]));
  if (err != NULL) {
    return err;
  }

  return NULL;
}
