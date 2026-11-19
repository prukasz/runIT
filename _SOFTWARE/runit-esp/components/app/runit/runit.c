#include "runit.h"

#include <esp_log.h>
#include "board_id.h"
#include "board_time.h"
#include "err_log.h"
#include "err_sink.h"
#include "errors.h"
#include "nvs_store.h"
#include "router_queue.h"
#include "runit_connectivity.h"
#include "runit_ids.h"
#include "sys_actions.h"
#include "sys_board.h"
#include "sys_device.h"
#include "sys_device_policy.h"
#include "sys_event.h"
#include "sys_event_stream.h"
#include "sys_log.h"
#include "sys_remote.h"
#include "sys_session.h"
#include "sys_system.h"
#include "sys_watch.h"

#define TAG __FILE_NAME__

// Every reported chain: the device's policy answers it, the log keeps and sends it, the remote device's peers hear it.
static void error_sink(err_h chain, const cmd_req_t* cmd) {
  err_h response = sys_device_policy_handle(chain);
  sys_log_report(chain, cmd);
  sys_remote_on_error(chain, cmd);
  if (response != NULL) {
    err_log(response);
    sys_log_report(response, NULL);
    err_free(response);
  }
}

void runit_start(void) {
  ESP_LOGI(TAG, "runIT rework: up, uptime %llu ms", (unsigned long long)board_time_now_ms());
  err_report(nvs_store_init("nvs"));
  err_report(board_id_init());
  err_report(sys_actions_init());
  err_report(sys_board_init());
  err_report(router_init());
  err_report(sys_event_init());
  err_report(sys_device_policy_init());
  err_report(sys_session_init());
  // The modules' ports before the links to them.
  err_report(sys_log_init());
  err_report(sys_event_stream_init());
  err_report(sys_watch_init());
  err_report(sys_remote_init());
  err_report(runit_data_matrix_setup());
  err_set_sink(error_sink);
  err_report(sys_system_init());
  err_report(sys_device_create_system(RUNIT_DEV_GPIO, RUNIT_DEV_TYPE_GPIO, NULL, 0u));
  err_report(sys_board_boot());  // the profile's devices, after the board's own: its frames name the GPIO's pins
}
