#include "selftest_harness.h"
#include "sys_actions.h"
#include "sys_device.h"
#include "vm_exec.h"

#define FAULT_TEST_DEVICE_ID 127
#define FAULT_ACTION_CRITICAL 13
#define FAULT_ACTION_WARNING 14
#define FAULT_ACTION_NOTICE 15

typedef struct {
  uint8_t device_id;
} fault_test_cfg_t;

typedef struct {
  uint32_t freezes;
  uint32_t actions[3];
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

static err_h fault_action_critical(void* arg) {
  (void)arg;
  s_fault_ctx.actions[SYS_DEV_ERR_CRITICAL]++;
  return NULL;
}

static err_h fault_action_warning(void* arg) {
  (void)arg;
  s_fault_ctx.actions[SYS_DEV_ERR_WARNING]++;
  return NULL;
}

static err_h fault_action_notice(void* arg) {
  (void)arg;
  s_fault_ctx.actions[SYS_DEV_ERR_NOTICE]++;
  return NULL;
}

static err_h fault_action_fail(void* arg) {
  (void)arg;
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
  bool installed = sys_device_install_cfg(&s_fault_test_class, cfg.device_id,
                                          &cfg, sizeof(cfg)) == NULL;
  ck("fault-policy test device installed", installed);
  if (!installed) return;

  (void)sys_actions_bind_static(FAULT_ACTION_CRITICAL, fault_action_critical, NULL);
  (void)sys_actions_bind_static(FAULT_ACTION_WARNING, fault_action_warning, NULL);
  (void)sys_actions_bind_static(FAULT_ACTION_NOTICE, fault_action_notice, NULL);
  const uint8_t actions[] = {FAULT_ACTION_CRITICAL, FAULT_ACTION_WARNING,
                             FAULT_ACTION_NOTICE};
  ck("device error actions configured",
     sys_device_set_error_handling(cfg.device_id, true, false, actions) == NULL);

  vm_exec_fault_acknowledge();
  vm_exec_set_mode(VM_RUN_RUNNING);
  err_h warning = SE_ERR_NEW(ERR_POWER_BUDGET_EXCEEDED, .device_id = cfg.device_id);
  ck("power-budget fault is warning",
     sys_device_classify_error(warning) == SYS_DEV_ERR_WARNING);
  ck("warning invokes only its configured action",
     sys_device_report_error(cfg.device_id, warning) == NULL &&
         s_fault_ctx.actions[SYS_DEV_ERR_WARNING] == 1 &&
         s_fault_ctx.actions[SYS_DEV_ERR_CRITICAL] == 0 &&
         !vm_exec_fault_status().latched && vm_exec_mode() == VM_RUN_RUNNING);

  err_h notice = SE_ERR_NEW(ERR_INVALID_VAL_UI32, .val = 9, .min = 0, .max = 1);
  ck("validation fault is notice",
     sys_device_classify_error(notice) == SYS_DEV_ERR_NOTICE);
  ck("notice invokes only its configured action",
     sys_device_report_error(cfg.device_id, notice) == NULL &&
         s_fault_ctx.actions[SYS_DEV_ERR_NOTICE] == 1 &&
         s_fault_ctx.actions[SYS_DEV_ERR_WARNING] == 1 &&
         !vm_exec_fault_status().latched && vm_exec_mode() == VM_RUN_RUNNING);

  err_h critical = SE_ERR_NEW(ERR_ESP_ERR, .esp_code = ESP_FAIL);
  ck("driver failure is critical",
     sys_device_classify_error(critical) == SYS_DEV_ERR_CRITICAL);
  ck("critical fault stops, freezes, invokes its action, and latches root",
     sys_device_report_error(cfg.device_id, critical) == NULL &&
         vm_exec_mode() == VM_RUN_STOPPED && vm_exec_fault_status().latched &&
         vm_exec_fault_status().device_id == cfg.device_id &&
         vm_exec_fault_status().root_tag == ERR_ESP_ERR &&
         s_fault_ctx.freezes == 1 &&
         s_fault_ctx.actions[SYS_DEV_ERR_CRITICAL] == 1);
  ck("repeated critical fault is counted without replaying the response",
     sys_device_report_error(cfg.device_id, critical) == NULL &&
         vm_exec_fault_status().occurrences == 2 && s_fault_ctx.freezes == 1 &&
         s_fault_ctx.actions[SYS_DEV_ERR_CRITICAL] == 1);

  ck("fault acknowledgment permits another episode",
     vm_exec_fault_acknowledge() == NULL && !vm_exec_fault_status().latched);
  s_fault_ctx.fail_freeze = true;
  err_h freeze_critical = SE_ERR_NEW(ERR_ESP_ERR, .esp_code = ESP_FAIL);
  err_h freeze_error = sys_device_report_error(cfg.device_id, freeze_critical);
  ck("safe-state failure is a structured non-recursive response error",
     fault_error_is(freeze_error, ERR_DEV_FAULT_RESPONSE_FAILED) &&
         ((err_payload_ERR_DEV_FAULT_RESPONSE_FAILED_t*)freeze_error->payload)->stage ==
             SYS_DEV_FAULT_STAGE_FREEZE &&
         s_fault_ctx.actions[SYS_DEV_ERR_CRITICAL] == 2);
  s_fault_ctx.fail_freeze = false;
  (void)vm_exec_fault_acknowledge();

  (void)sys_actions_bind_static(FAULT_ACTION_NOTICE, fault_action_fail, NULL);
  err_h action_notice = SE_ERR_NEW(ERR_INVALID_VAL_UI32, .val = 9, .min = 0, .max = 1);
  err_h action_error = sys_device_report_error(cfg.device_id, action_notice);
  ck("configured action failure is reported structurally",
     fault_error_is(action_error, ERR_DEV_FAULT_RESPONSE_FAILED) &&
         ((err_payload_ERR_DEV_FAULT_RESPONSE_FAILED_t*)action_error->payload)->stage ==
             SYS_DEV_FAULT_STAGE_ACTION);

  (void)sys_actions_bind_static(FAULT_ACTION_CRITICAL, NULL, NULL);
  (void)sys_actions_bind_static(FAULT_ACTION_WARNING, NULL, NULL);
  (void)sys_actions_bind_static(FAULT_ACTION_NOTICE, NULL, NULL);
  ck("fault-policy test device uninstalled",
     sys_device_uninstall(cfg.device_id) == NULL &&
         sys_device_get_by_id(cfg.device_id) == NULL);
  (void)vm_exec_stop();
}
