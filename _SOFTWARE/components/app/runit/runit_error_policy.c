/*
 * The app's side of the error policy (the policy itself is sys_device's, SYS_DEVICE.MD "Error policy"): the action
 * runner it uses, and the error sink that runs it for every reported chain.
 */
#include "runit_error_policy.h"
#include <esp_log.h>
#include "router.h"
#include "sdkconfig.h"
#include "sys_actions.h"
#include "sys_connector.h"
#include "sys_device.h"
#include "sys_log.h"
#include "vm_router.h"

#define TAG "runit_policy"

#define ERR_RECORD_MAX 192u  // one error record on the errors stream (built on the reporter's stack)

// The action already failed in this chain: running it again would answer its own failure forever.
static bool action_failed_in(err_h chain, uint32_t action) {
  uint32_t left = CONFIG_ERRORS_POOL_BLOCKS + 1;
  for (err_h n = chain; n != NULL && left-- > 0; n = n->next) {
    if (n->tag == ERR_ACTION_STEP && ERR_DATA(n, ERR_ACTION_STEP)->id == action) {
      return true;
    }
  }
  return false;
}

// A device's response action, queued on the router (an action runs on a router task only).
static err_h run_action(uint32_t action, err_h chain) {
  if (action_failed_in(chain, action)) {
    ESP_LOGW(TAG, "action %08lx not run again (its own failure)", (unsigned long)action);
    return NULL;
  }
  return sys_actions_request(action, false);
}

static void send_record(err_h chain, const void* origin) {
  if (!sys_log_send_errors()) {
    return;
  }
  uint8_t rec[ERR_RECORD_MAX];
  uint32_t len = router_err_record(origin, chain, rec, sizeof(rec));
  err_free(sys_connector_send(SYS_CONNECTOR_ERRORS, rec, len));
}

/* Every reported chain: the device policy (whatever send_errors says), then an error record on the errors stream;
 * `origin` is the router command when it came from one. A failed response is logged and sent, not reported (that
 * would run the policy on it). A full queue drops a record: reporting that would loop. */
static void error_sink(err_h chain, const void* origin) {
  err_h response = sys_device_handle_error(chain);
  send_record(chain, origin);
  if (response != NULL) {
    err_log(response);
    send_record(response, NULL);
    err_free(response);
  }
}

err_h runit_error_wiring_init(void) {
  sys_device_set_action_runner(run_action);
  sys_device_set_vm_stop(vm_stop_on_fault);
  err_set_sink(error_sink);
  return NULL;
}
