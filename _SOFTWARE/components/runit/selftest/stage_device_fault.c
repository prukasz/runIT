#include "selftest_harness.h"
#include "sys_actions.h"
#include "sys_device.h"
#include "vm_exec.h"

#define FAULT_TEST_DEVICE_ID 127
#define FAULT_ACTION_LOW 12
#define FAULT_ACTION_MEDIUM 13
#define FAULT_ACTION_HIGH 14
#define FAULT_ACTION_CRITICAL 15

typedef struct {
  uint8_t device_id;
} fault_test_cfg_t;

typedef struct {
  uint32_t freezes;
  uint32_t actions[5];
  bool fail_freeze;
} fault_test_ctx_t;

static fault_test_ctx_t s_fault_ctx;

static err_h fault_test_install(const void* cfg, void** out_handle) {
  (void)cfg;
  memset(&s_fault_ctx, 0, sizeof(s_fault_ctx));
  *out_handle = &s_fault_ctx;
  return NULL;
}

static err_h fault_test_uninstall(void* handle) {
  (void)handle;
  return NULL;
}

static err_h fault_test_freeze(void* handle) {
  fault_test_ctx_t* ctx = (fault_test_ctx_t*)handle;
  ctx->freezes++;
  if (ctx->fail_freeze) SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
  return NULL;
}

static const sys_device_class_t s_fault_test_class = {
    .name = "fault-test",
    .ops = {.install = fault_test_install,
            .uninstall = fault_test_uninstall,
            .freeze = fault_test_freeze},
};

static err_h fault_action_low(void) {
  s_fault_ctx.actions[SYS_DEV_ERR_LOW]++;
  return NULL;
}

static err_h fault_action_medium(void) {
  s_fault_ctx.actions[SYS_DEV_ERR_MEDIUM]++;
  return NULL;
}

static err_h fault_action_high(void) {
  s_fault_ctx.actions[SYS_DEV_ERR_HIGH]++;
  return NULL;
}

static err_h fault_action_critical(void) {
  s_fault_ctx.actions[SYS_DEV_ERR_CRITICAL]++;
  return NULL;
}

static err_h fault_action_fail(void) {
  SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
}

static bool fault_error_is(err_h error, err_tag_e tag) {
  for (err_h node = error; node; node = node->next_cause) {
    if (node->tag == tag) return true;
  }
  return false;
}

void test_device_fault_policy(void) {
  ESP_LOGI(TAG, "-- device fault policy --");
  const fault_test_cfg_t cfg = {.device_id = FAULT_TEST_DEVICE_ID};
  bool installed = selftest_ok(sys_device_install_cfg(&s_fault_test_class, cfg.device_id,
                                          &cfg, sizeof(cfg)));
  ck("fault-policy test device installed", installed);
  if (!installed) return;

  SE_release(sys_actions_bind_static(FAULT_ACTION_LOW, fault_action_low));
  SE_release(sys_actions_bind_static(FAULT_ACTION_MEDIUM, fault_action_medium));
  SE_release(sys_actions_bind_static(FAULT_ACTION_HIGH, fault_action_high));
  SE_release(sys_actions_bind_static(FAULT_ACTION_CRITICAL, fault_action_critical));
  const uint8_t actions[] = {0, FAULT_ACTION_LOW, FAULT_ACTION_MEDIUM,
                             FAULT_ACTION_HIGH, FAULT_ACTION_CRITICAL};
  ck("device error actions configured",
     selftest_ok(sys_device_set_error_handling(cfg.device_id, SYS_DEV_IMPORTANCE_CRITICAL, actions)));

  SE_release(vm_exec_fault_acknowledge());
  vm_exec_set_mode(VM_RUN_RUNNING);
  err_h low = SE_ERR_NEW(ERR_INVALID_VAL_UI32, .val = 9, .min = 0, .max = 1);
  ck("low fault invokes only its configured action",
     selftest_ok(sys_device_report_error(cfg.device_id, low)) &&
         s_fault_ctx.actions[SYS_DEV_ERR_LOW] == 1 &&
         s_fault_ctx.actions[SYS_DEV_ERR_CRITICAL] == 0 &&
         !vm_exec_fault_status().latched && vm_exec_mode() == VM_RUN_RUNNING);

  err_h medium = SE_ERR_NEW(ERR_DEV_SUSPENDED, .dev_id = cfg.device_id);
  ck("medium fault invokes only its configured action",
     selftest_ok(sys_device_report_error(cfg.device_id, medium)) &&
         s_fault_ctx.actions[SYS_DEV_ERR_MEDIUM] == 1 &&
         s_fault_ctx.actions[SYS_DEV_ERR_LOW] == 1 &&
         !vm_exec_fault_status().latched && vm_exec_mode() == VM_RUN_RUNNING);

  err_h high = SE_ERR_NEW(ERR_POWER_BUDGET_EXCEEDED, .device_id = cfg.device_id);
  ck("high fault invokes only its configured action",
     selftest_ok(sys_device_report_error(cfg.device_id, high)) &&
         s_fault_ctx.actions[SYS_DEV_ERR_HIGH] == 1 &&
         s_fault_ctx.actions[SYS_DEV_ERR_MEDIUM] == 1 &&
         !vm_exec_fault_status().latched && vm_exec_mode() == VM_RUN_RUNNING);

  err_h critical = SE_ERR_NEW(ERR_ESP_ERR, .esp_code = ESP_FAIL);
  ck("critical fault stops, freezes, invokes its action, and latches root",
     selftest_ok(sys_device_report_error(cfg.device_id, critical)) &&
         vm_exec_mode() == VM_RUN_STOPPED && vm_exec_fault_status().latched &&
         vm_exec_fault_status().device_id == cfg.device_id &&
         vm_exec_fault_status().root_tag == ERR_ESP_ERR &&
         s_fault_ctx.freezes == 1 &&
         s_fault_ctx.actions[SYS_DEV_ERR_CRITICAL] == 1);
  ck("repeated critical fault skips shutdown but runs its device action",
     selftest_ok(sys_device_report_error(cfg.device_id, critical)) &&
         vm_exec_fault_status().occurrences == 2 && s_fault_ctx.freezes == 1 &&
         s_fault_ctx.actions[SYS_DEV_ERR_CRITICAL] == 2);

  ck("fault acknowledgment permits another episode",
     selftest_ok(vm_exec_fault_acknowledge()) && !vm_exec_fault_status().latched);
  s_fault_ctx.fail_freeze = true;
  err_h freeze_critical = SE_ERR_NEW(ERR_ESP_ERR, .esp_code = ESP_FAIL);
  err_h freeze_error = sys_device_report_error(cfg.device_id, freeze_critical);
  ck("safe-state failure is a structured non-recursive response error",
     fault_error_is(freeze_error, ERR_DEV_FAULT_RESPONSE_FAILED) &&
         ((err_payload_ERR_DEV_FAULT_RESPONSE_FAILED_t*)freeze_error->payload)->stage ==
             SYS_DEV_FAULT_STAGE_FREEZE &&
         s_fault_ctx.actions[SYS_DEV_ERR_CRITICAL] == 3);
  s_fault_ctx.fail_freeze = false;
  SE_release(vm_exec_fault_acknowledge());

  /* Test non-critical clamping: device with LOW importance clamps HIGH down to LOW */
  ck("reconfigure device importance to LOW",
     selftest_ok(sys_device_set_error_handling(cfg.device_id, SYS_DEV_IMPORTANCE_LOW, actions)));
  vm_exec_set_mode(VM_RUN_RUNNING);
  uint32_t low_before = s_fault_ctx.actions[SYS_DEV_ERR_LOW];
  ck("high error clamped to LOW importance invokes only LOW action",
     selftest_ok(sys_device_report_error(cfg.device_id, high)) &&
         s_fault_ctx.actions[SYS_DEV_ERR_LOW] == low_before + 1 &&
         !vm_exec_fault_status().latched && vm_exec_mode() == VM_RUN_RUNNING);

  /* Test importance NONE disables non-critical error reporting */
  ck("reconfigure device importance to NONE",
     selftest_ok(sys_device_set_error_handling(cfg.device_id, SYS_DEV_IMPORTANCE_NONE, actions)));
  uint32_t low_count = s_fault_ctx.actions[SYS_DEV_ERR_LOW];
  ck("disabled device drops non-critical errors",
     selftest_ok(sys_device_report_error(cfg.device_id, low)) &&
         s_fault_ctx.actions[SYS_DEV_ERR_LOW] == low_count &&
         vm_exec_mode() == VM_RUN_RUNNING);

  /* Importance NONE (test device): ignores all error handling, including CRITICAL */
  err_h crit_none = SE_ERR_NEW(ERR_ESP_ERR, .esp_code = ESP_FAIL);
  ck("critical error on NONE importance device is ignored (no halt, no latch)",
     selftest_ok(sys_device_report_error(cfg.device_id, crit_none)) &&
         vm_exec_mode() == VM_RUN_RUNNING && !vm_exec_fault_status().latched);

  /* Re-enable for action failure test */
  SE_release(sys_device_set_error_handling(cfg.device_id, SYS_DEV_IMPORTANCE_HIGH, actions));
  SE_release(sys_actions_bind_static(FAULT_ACTION_LOW, fault_action_fail));
  err_h action_low = SE_ERR_NEW(ERR_INVALID_VAL_UI32, .val = 9, .min = 0, .max = 1);
  err_h action_error = sys_device_report_error(cfg.device_id, action_low);
  ck("configured action failure is reported structurally",
     fault_error_is(action_error, ERR_DEV_FAULT_RESPONSE_FAILED) &&
         ((err_payload_ERR_DEV_FAULT_RESPONSE_FAILED_t*)action_error->payload)->stage ==
             SYS_DEV_FAULT_STAGE_ACTION);

  SE_release(low);
  SE_release(medium);
  SE_release(high);
  SE_release(critical);
  SE_release(freeze_critical);
  SE_release(freeze_error);
  SE_release(crit_none);
  SE_release(action_low);
  SE_release(action_error);

  SE_release(sys_actions_bind_static(FAULT_ACTION_LOW, NULL));
  SE_release(sys_actions_bind_static(FAULT_ACTION_MEDIUM, NULL));
  SE_release(sys_actions_bind_static(FAULT_ACTION_HIGH, NULL));
  SE_release(sys_actions_bind_static(FAULT_ACTION_CRITICAL, NULL));
  ck("fault-policy test device uninstalled",
     selftest_ok(sys_device_uninstall(cfg.device_id)) &&
         sys_device_get_by_id(cfg.device_id) == NULL);
  SE_release(vm_exec_stop());
}
