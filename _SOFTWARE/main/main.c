#include <esp_log.h>
#include "errors.h"
#include "router.h"
#include "sys_actions.h"
#include "sys_actions_router.h"
#include "sys_ble.h"
#include "sys_ble_link.h"
#include "sys_ble_router.h"
#include "sys_connector.h"
#include "sys_connector_router.h"
#include "sys_device.h"
#include "sys_device_router.h"
#include "sys_event.h"
#include "sys_event_router.h"
#include "sys_i2c.h"
#include "sys_i2c_router.h"
#include "sys_uart.h"
#include "utils.h"

#define TAG __FILE_NAME__

#define ERR_RECORD_MAX 192u  // one error record on the errors stream (built on the reporter's stack)

// Safe state = every device suspended (outputs off). The VM joins here once it is ported.
static void safe_state(router_safe_reason_e reason) {
  ESP_LOGW(TAG, "safe state (reason %d): suspending devices", (int)reason);
  err_report(sys_device_suspend_all());
}

/* Every reported chain goes to the errors stream as an error record; `origin`
 * is the router command when it came from one. A full queue drops it:
 * reporting that would loop. */
static void error_sink(err_h chain, const void* origin) {
  uint8_t rec[ERR_RECORD_MAX];
  uint32_t len = router_err_record(origin, chain, rec, sizeof(rec));
  err_free(sys_connector_send(SYS_CONNECTOR_ERRORS, rec, len));
}

static err_h streams_init(void) {
  ERR_TRY(sys_connector_init());
  err_set_sink(error_sink);
  return sys_connector_capture_logs(SYS_CONNECTOR_LOGS);
}

static err_h router_setup(void) {
  ERR_TRY(router_register(ROUTER_KEY_I2C, sys_i2c_router));
  ERR_TRY(router_register(ROUTER_KEY_BLE, sys_ble_router));
  ERR_TRY(router_register(ROUTER_KEY_CONN, sys_connector_router));
  ERR_TRY(router_register(ROUTER_KEY_DEVICE, sys_device_router));
  ERR_TRY(router_register(ROUTER_KEY_ACTIONS, sys_actions_router));
  ERR_TRY(router_register(ROUTER_KEY_EVENTS, sys_event_router));
  router_set_tap(&g_sys_actions_tap);
  sys_event_set_action_runner(sys_actions_request);  // rules run actions through the router
  router_set_safe_state(safe_state);
  return router_init();
}

/* Same routing as the legacy board: logs and errors on BLE LOGS, telemetry on BLE TX; errors and telemetry on UART.
 * Events (new) next to telemetry: BLE TX and UART. */
static err_h stream_bindings(void) {
  ERR_TRY(sys_connector_bind(SYS_CONNECTOR_ERRORS, TRANSPORT_UART, SYS_UART_ENDPOINT));
  ERR_TRY(sys_connector_bind(SYS_CONNECTOR_TELEMETRY, TRANSPORT_UART, SYS_UART_ENDPOINT));
  ERR_TRY(sys_connector_bind(SYS_CONNECTOR_EVENTS, TRANSPORT_UART, SYS_UART_ENDPOINT));
  ERR_TRY(sys_connector_bind(SYS_CONNECTOR_LOGS, TRANSPORT_BLE, SYS_BLE_CHR_RUNIT_LOGS));
  ERR_TRY(sys_connector_bind(SYS_CONNECTOR_ERRORS, TRANSPORT_BLE, SYS_BLE_CHR_RUNIT_LOGS));
  ERR_TRY(sys_connector_bind(SYS_CONNECTOR_TELEMETRY, TRANSPORT_BLE, SYS_BLE_CHR_RUNIT_TX));
  ERR_TRY(sys_connector_bind(SYS_CONNECTOR_EVENTS, TRANSPORT_BLE, SYS_BLE_CHR_RUNIT_TX));
  return NULL;
}

typedef struct {
  const char* name;
  err_h (*fn)(void);
} boot_step_t;

/* In order. A failing step is reported and boot goes on, so one missing
 * piece (no BLE controller under QEMU, no I2C pull-ups) doesn't take the
 * command links down with it. */
static const boot_step_t k_boot[] = {
    {"streams", streams_init},          {"actions", sys_actions_init},
    {"events", sys_event_init},         {"router", router_setup},
    {"uart", sys_uart_init},            {"ble link", sys_ble_link_init},
    {"stream bindings", stream_bindings}, {"i2c", sys_i2c_init},
#if CONFIG_RUNIT_BLE_START
    {"ble", sys_ble_start},
#endif
};

void app_main(void) {
  uint32_t failed = 0;
  for (size_t i = 0; i < ARRAY_LEN(k_boot); i++) {
    err_h err = k_boot[i].fn();
    if (err != NULL) {
      ESP_LOGE(TAG, "boot step '%s' failed", k_boot[i].name);
      err_report(err);
      failed++;
    }
  }
  ESP_LOGI(TAG, "runIT up (%lu boot step(s) failed)", (unsigned long)failed);
  while (1) {
    vTaskDelay(WAIT_FOREVER);
  }
}
