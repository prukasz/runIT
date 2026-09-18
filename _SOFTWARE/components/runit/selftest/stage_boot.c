#include "selftest_harness.h"
#include "runit.h"
#include "runit_board_cfg.h"
#include "sys_error.h"
#include "vm_exec.h"

#undef OWNER
#define OWNER OWNER_VM_BASE

static int s_mock_step1_count = 0;
static int s_mock_step2_count = 0;
static int s_mock_step3_count = 0;
static bool s_mock_step2_fail = false;

static err_h mock_step1(void) {
  s_mock_step1_count++;
  return NULL;
}

static err_h mock_step2(void) {
  s_mock_step2_count++;
  if (s_mock_step2_fail) {
    SE_RET_ERR(ERR_ESP_ERR, .esp_code = 0x55);
  }
  return NULL;
}

static err_h mock_step3(void) {
  s_mock_step3_count++;
  return NULL;
}

void test_boot_failure_policy(void) {
  ESP_LOGI(TAG, "--- Coherent boot failure propagation and safe state tests ---");

  // 1. Success path: all steps run when no error occurs
  s_mock_step1_count = 0;
  s_mock_step2_count = 0;
  s_mock_step3_count = 0;
  s_mock_step2_fail = false;

  const runit_boot_step_entry_t success_steps[] = {
      {"mock1", mock_step1},
      {"mock2", mock_step2},
      {"mock3", mock_step3},
  };

  err_h err = runit_run_boot_steps(success_steps, 3);
  ck("all boot steps succeed and return NULL", err == NULL);
  ck("step 1 executed", s_mock_step1_count == 1);
  ck("step 2 executed", s_mock_step2_count == 1);
  ck("step 3 executed", s_mock_step3_count == 1);

  // 2. Failure path: error at step 2 prevents step 3 and enters safe state
  s_mock_step1_count = 0;
  s_mock_step2_count = 0;
  s_mock_step3_count = 0;
  s_mock_step2_fail = true;

  // Set VM to running before test to ensure runner halts it on failure
  vm_exec_set_mode(VM_RUN_RUNNING);
  ck("VM running before failing boot step", vm_exec_mode() == VM_RUN_RUNNING);

  err = runit_run_boot_steps(success_steps, 3);
  ck("failing step returns error", err != NULL && err->tag == ERR_ESP_ERR);
  SE_release(err);
  ck("step 1 executed before failure", s_mock_step1_count == 1);
  ck("step 2 executed and failed", s_mock_step2_count == 1);
  ck("step 3 was prevented from running", s_mock_step3_count == 0);
  ck("safe state entered on failure (VM stopped)", vm_exec_mode() == VM_RUN_STOPPED);

  // 3. Forced failure at step 0 prevents all subsequent steps
  s_mock_step1_count = 0;
  s_mock_step2_count = 0;
  s_mock_step3_count = 0;
  const runit_boot_step_entry_t early_fail_steps[] = {
      {"failing_step0", mock_step2}, // s_mock_step2_fail is still true
      {"dependent_step1", mock_step1},
      {"dependent_step2", mock_step3},
  };

  err = runit_run_boot_steps(early_fail_steps, 3);
  ck("early failing step returns error", err != NULL);
  SE_release(err);
  ck("dependent step 1 prevented", s_mock_step1_count == 0);
  ck("dependent step 2 prevented", s_mock_step3_count == 0);

  // 4. Test runit_enter_safe_state() directly
  vm_exec_set_mode(VM_RUN_RUNNING);
  runit_enter_safe_state();
  ck("runit_enter_safe_state stops VM", vm_exec_mode() == VM_RUN_STOPPED);

  // 5. sys_power_static_config() error propagation test
  // Power limits were already configured and locked during real boot.
  // Calling sys_power_static_config() again must NOT be swallowed or log success;
  // it must return ERR_DEP_FAILED wrapping ERR_BASE_INVALID_STATE.
  err_h p_err = sys_power_static_config();
  ck("sys_power_static_config propagates error when limits locked", p_err != NULL && p_err->tag == ERR_BASE_INVALID_STATE);
  SE_release(p_err);

  // Leave VM clean and stopped
  SE_release(vm_exec_stop());
  vm_exec_set_sample_hook(NULL);
}
