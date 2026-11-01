/*
 * The app's side of the error policy (the policy itself is sys_device's, SYS_DEVICE.MD "Error policy"): the action
 * runner it uses, and the error sink that runs it for every reported chain.
 */
#include "runit_error_policy.h"
#include <esp_log.h>
#include "sdkconfig.h"
#include "sys_actions.h"
#include "sys_device.h"
#include "sys_event.h"
#include "runit.h"
#include "sys_log.h"
#include "sys_session.h"
#include "vm_exec.h"
#include "vm_router.h"

#define TAG __FILE_NAME__

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

// The record is built on the reporter's stack; sys_log keeps it and sends it.
static void send_record(err_h chain, const err_cmd_t* cmd) {
  uint8_t rec[SYS_LOG_RECORD_MAX];
  sys_log_error(rec, err_record(cmd, chain, rec, sizeof(rec)));
}

/* Every reported chain: the device policy (whatever send_errors says), then an error record on the errors stream;
 * `cmd` is the router command when it came from one. A failed response is logged and sent, not reported (that
 * would run the policy on it). A full queue drops a record: reporting that would loop. */
static void error_sink(err_h chain, const err_cmd_t* cmd) {
  err_h response = sys_device_handle_error(chain);
  send_record(chain, cmd);
  if (response != NULL) {
    err_log(response);
    send_record(response, NULL);
    err_free(response);
  }
}

/* A device's `suspended` flag changed (or every device's, after a sweep: source SYS_DEVICE_ID_NONE): a DEVICE event with
 * NOTIFY, so the app knows of a safe state or a fault response without asking. Under the registry lock: the event goes out
 * inline (the events connector, then the listeners: none of them waits for a device lock held elsewhere). */
static void device_state_changed(uint16_t id, bool suspended) {
  const err_h err = suspended ? SYS_EVENT_PUBLISH(SYS_DEVICE, SUSPENDED, id, 0, 0)
                              : SYS_EVENT_PUBLISH(SYS_DEVICE, RESUMED, id, 0, 0);
  if (err != NULL) {
    err_report(err);
  }
}

static uint8_t s_board_policy[SYS_SESSION_ID_LAST + 1u];  // runit_board_policy_e per board, zero = log only

err_h runit_board_policy_set(uint8_t board, runit_board_policy_e policy) {
  ERR_CHECK(board <= SYS_SESSION_ID_LAST && policy <= RUNIT_BOARD_POLICY_SAFE, ERR_CMD_BAD_ARG);
  s_board_policy[board] = (uint8_t)policy;
  return NULL;
}

/* An error about a board as a whole: the record on the errors stream, then the board's policy. */
static void board_error_sink(err_h chain, uint8_t board) {
  uint8_t rec[SYS_LOG_RECORD_MAX];
  sys_log_error(rec, err_record_board(board, chain, rec, sizeof(rec)));
  switch (board <= SYS_SESSION_ID_LAST ? s_board_policy[board] : 0) {
    case RUNIT_BOARD_POLICY_STOP_VM:
      err_report(vm_exec_stop());
      break;
    case RUNIT_BOARD_POLICY_SAFE:
      runit_enter_safe_state();
      break;
    default:
      break;
  }
}

err_h runit_error_wiring_init(void) {
  sys_device_set_state_listener(device_state_changed);
  sys_device_set_action_runner(run_action);
  sys_device_set_vm_stop(vm_stop_on_fault);
  err_set_sink(error_sink);
  err_set_board_sink(board_error_sink);
  return NULL;
}
