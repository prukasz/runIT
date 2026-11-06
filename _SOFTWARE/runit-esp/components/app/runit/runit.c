#include "runit.h"
#include <esp_log.h>
#include "router.h"
#include "runit_board_cfg.h"
#include "runit_error_policy.h"
#include "sdkconfig.h"
#include "sys_actions.h"
#include "sys_actions_router.h"
#include "sys_ble.h"
#include "sys_ble_link.h"
#include "sys_ble_router.h"
#include "sys_board.h"
#include "sys_board_router.h"
#include "sys_data_provider_driver.h"
#include "sys_data_provider_router.h"
#include "sys_device.h"
#include "sys_device_router.h"
#include "sys_event.h"
#include "sys_i2c.h"
#include "sys_i2c_router.h"
#include "sys_log.h"
#include "sys_log_router.h"
#include "sys_nvs.h"
#include "sys_power_manager_router.h"
#include "sys_ota.h"
#include "sys_ota_router.h"
#include "sys_project.h"
#include "sys_project_router.h"
#include "sys_session.h"
#include "sys_session_router.h"
#include "sys_stream.h"
#include "sys_stream_router.h"
#include "sys_system.h"
#include "sys_system_router.h"
#include "sys_uart.h"
#if CONFIG_RUNIT_LINK_ROOT || CONFIG_RUNIT_LINK_CHILD
#include "sys_remote.h"
#include "sys_remote_router.h"
#endif
#include "sys_watch.h"
#include "sys_watch_router.h"
#include "vm_exec.h"
#include "vm_retain.h"
#include "vm_router.h"
#include "utils.h"

#define TAG __FILE_NAME__

void runit_enter_safe_state(void) {
  ESP_LOGW(TAG, "safe state: stopping the VM, suspending devices");
  err_report(vm_exec_stop());  // the program stops writing to the devices; its retained values are saved
  vm_retain_save_stopped();
  err_report(sys_device_sweep(SYS_DEVICE_OP_SUSPEND));
}

// The safe state of sys_system (STOP, dead-man, a power fault): sys_system notes why, for SYS SAFE_GET.
static void safe_state(sys_safe_reason_e reason) {
  ESP_LOGW(TAG, "safe state (reason %d)", (int)reason);
  runit_enter_safe_state();
}

// SYS SAFE_LEAVE: the way out is the way in, reversed; the program is started again by the client (VM EXEC).
static err_h safe_leave(void) {
  return sys_device_sweep(SYS_DEVICE_OP_RESUME);
}

void runit_power_safe_state(void) {
  sys_system_enter_safe_state(SYS_SAFE_POWER);
}

// Every NVS partition: one failing leaves the other usable.
static err_h nvs_init(void) {
  static const char* const k_partitions[] = {NVS_DEFAULT_PART_NAME, CONFIG_SYS_ACTIONS_PARTITION,
                                             CONFIG_SYS_PROJECT_SOURCE_PARTITION};
  err_h first = NULL;
  for (size_t i = 0; i < ARRAY_LEN(k_partitions); i++) {
    err_h err = sys_nvs_init(k_partitions[i]);
    if (first == NULL) {
      first = err;
    } else {
      err_report(err);
    }
  }
  return first;
}

// The streams (they listen for the session's gone peers) and the board's own, then the error policy and sink that
// send to them.
static err_h streams_init(void) {
  ERR_TRY(sys_stream_init());
  ERR_TRY(runit_board_streams_init());
  return runit_error_wiring_init();
}

// ESP_LOG to the console and the logs stream, with the stored options.
static err_h log_init(void) {
  return sys_log_init();
}

// A firmware write stalls what runs from flash, so no update while a program runs.
static bool vm_runs(void) {
  return vm_exec_mode() != VM_RUN_STOPPED;
}

static err_h router_setup(void) {
  ERR_TRY(router_register(ROUTER_KEY_SYS, sys_system_router));
  ERR_TRY(router_register(ROUTER_KEY_I2C, sys_i2c_router));
  ERR_TRY(router_register(ROUTER_KEY_BLE, sys_ble_router));
  ERR_TRY(router_register(ROUTER_KEY_STREAM, sys_stream_router));
  ERR_TRY(router_register(ROUTER_KEY_DEVICE, sys_device_router));
  ERR_TRY(router_register(ROUTER_KEY_ACTIONS, sys_actions_router));
  ERR_TRY(router_register(ROUTER_KEY_LOG, sys_log_router));
  ERR_TRY(router_register(ROUTER_KEY_POWER, sys_power_manager_router));
  ERR_TRY(router_register(ROUTER_KEY_VM, vm_router));
  ERR_TRY(router_register(ROUTER_KEY_PROJECT, sys_project_router));
  ERR_TRY(router_register(ROUTER_KEY_BOARD, sys_board_router));
  ERR_TRY(router_register(ROUTER_KEY_OTA, sys_ota_router));
  sys_ota_set_guard(vm_runs);
  ERR_TRY(router_register(ROUTER_KEY_PROVIDER, sys_data_provider_router));
  ERR_TRY(router_register(ROUTER_KEY_SESSION, sys_session_router));
  ERR_TRY(router_register(ROUTER_KEY_WATCH, sys_watch_router));
#if CONFIG_RUNIT_LINK_ROOT || CONFIG_RUNIT_LINK_CHILD
  ERR_TRY(router_register(ROUTER_KEY_REMOTE, sys_remote_router));
#endif
  sys_system_set_safe_state(safe_state);
  sys_system_set_safe_leave(safe_leave);
  ERR_TRY(sys_system_init());
  return router_init();
}

// The board's clients: the console's port and the runIT BLE pair, each a session; the providers so far are the board's.
static err_h sessions_init(void) {
#if CONFIG_RUNIT_LINK_CHILD
  // A board with a master takes no client: the app reaches it through the root.
  ERR_TRY(sys_session_attach(SYS_UART_LINK_PROVIDER, SYS_SESSION_MASTER));
#else
  ERR_TRY(sys_session_attach(SYS_UART_PROVIDER, SYS_SESSION_CLIENT));
  ERR_TRY(sys_session_attach(SYS_BLE_PROVIDER_RUNIT, SYS_SESSION_CLIENT));
#if CONFIG_RUNIT_LINK_ROOT
  ERR_TRY(sys_session_attach(SYS_UART_LINK_PROVIDER, SYS_SESSION_SLAVE));
#endif
#endif
  return NULL;
}

err_h runit_run_boot_steps(const runit_boot_step_entry_t* steps, size_t count, uint32_t* failed) {
  err_h first = NULL;
  for (size_t i = 0; i < count; i++) {
    err_h err = steps[i].fn();
    if (err == NULL) {
      continue;
    }
    ESP_LOGE(TAG, "boot step '%s' failed", steps[i].name);
    if (failed != NULL) {
      (*failed)++;
    }
    if (first == NULL) {
      first = err;
    } else {
      err_report(err);
    }
  }
  return first;
}

err_h runit_start(void) {
  static const runit_boot_step_entry_t k_boot[] = {
      {"nvs", nvs_init},              // first: the modules below only open their namespaces
      {"session", sys_session_init},  // before the streams and the dead-man: they listen for its gone peers
      {"streams", streams_init},
      {"log", log_init},
      {"actions", sys_actions_init},
      {"project", sys_project_init},
      {"board", sys_board_init},
      {"events", sys_event_init},
      {"watches", sys_watch_init},  // after the session and the events: it listens for gone peers and subscribes to events
#if CONFIG_RUNIT_LINK_ROOT || CONFIG_RUNIT_LINK_CHILD
      {"remote devices", sys_remote_init},  // after the session, watches and events it carries a device's calls on
#endif
      {"power", runit_board_power_init},  // before the router: it guards POWER_VREG
      {"router", router_setup},
      {"vm", vm_init},  // after the router: the program is loaded and run through ROUTER_KEY_VM
      {"uart", sys_uart_init},
      {"ble link", sys_ble_link_init},
      {"stream bindings", runit_board_stream_bindings_init},
      {"sessions", sessions_init},
      {"esp32 gpio", runit_board_gpio_init},  // the profile's frames use its pins
      {"board at start", sys_board_boot},  // before the project: its devices may use the board's
#if CONFIG_RUNIT_BLE_START
      {"ble", sys_ble_start},
#endif
      {"project at start", sys_project_boot},  // last: the load or run is queued at once and needs the devices
  };
  uint32_t failed = 0;
  err_h err = runit_run_boot_steps(k_boot, ARRAY_LEN(k_boot), &failed);
  ESP_LOGI(TAG, "runIT up (%lu boot step(s) failed)", (unsigned long)failed);
  return err;
}
