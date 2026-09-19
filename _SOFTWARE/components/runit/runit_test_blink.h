#pragma once

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "runit_board_defs.h"
#include "sys_error.h"
#include "sys_io.h"
#include "vm_block.h"
#include "vm_block_build.h"
#include "vm_block_timer.h"
#include "vm_block_io_toggle.h"
#include "vm_blocks.h"
#include "vm_exec.h"
#include "vm_obj_build.h"
#include "vm_store.h"

/**
 * @brief Sets up a 2-block VM program that blinks an LED on GPIO 7 at 1 Hz (500 ms period).
 *
 * Topology:
 * - Obj 0: Boolean signal (used as timer feedback and toggle enable)
 * - Acc 0: Accessor to Obj 0 (for Timer IN)
 * - Acc 1: Accessor to Obj 0 (for Toggle EN)
 * - Block 0 (VM_BLK_TIMER): Mode TON_INV, 500 ms preset.
 *   IN wired to Acc 0, Q wired to Obj 0. Auto-resets every 500 ms, producing a
 *   rising edge on each reset cycle.
 * - Block 1 (VM_BLK_IO_TOGGLE): Target DEVICE_ID_GPIO_ESP, pin 7.
 *   EN wired to Acc 1. Toggles GPIO 7 on every rising edge of EN (every 500 ms).
 */
static void runit_scan_metrics_task(void* arg);

static inline err_h runit_test_blink_setup(void) {
  ESP_LOGI("runit_blink", "Configuring 4-block VM LED blink program (GPIO 5, 6, 7 in parallel, 500ms)...");

  // 0. Set device importance to CRITICAL so critical faults trigger emergency shutdown
  sys_device_set_error_handling(DEVICE_ID_GPIO_ESP, SYS_DEV_IMPORTANCE_CRITICAL, NULL);

  // 1. Reset VM store and allocate arena & registries
  vm_store_reset();
  const uint16_t counts[VM_REG_CNT] = {
      [VM_REG_OBJ] = 2,
      [VM_REG_ACC] = 4,
      [VM_REG_BLK] = 4,
  };
  SE_ORIGIN_CALL(vm_store_open(4096, counts));

  // 2. Create Object 0: Boolean signal for timer feedback & toggle trigger
  vm_obj_head_t h = {0};
  h.payload_size = sizeof(uint8_t);
  h.d.obj_t = VM_OBJ_B;
  h.f.mutable = 1;
  vm_obj_h obj0 = NULL;
  SE_ORIGIN_CALL(vm_obj_create(&obj0, 0, &h, "pulse"));
  *(uint8_t*)obj0->payload = 1;

  // Object 1: Block 1 ENO output (Boolean, interlocks downstream Block 2)
  vm_obj_h obj1 = NULL;
  SE_ORIGIN_CALL(vm_obj_create(&obj1, 1, &h, "b1_eno"));
  *(uint8_t*)obj1->payload = 0;

  // 3. Create Accessors
  // Acc 0 -> Obj 0 (Timer IN)
  vm_accessor_t* acc0 = NULL;
  SE_ORIGIN_CALL(vm_accessor_create(&acc0, 0, 0, 0));
  (void)vm_accessor_cache_build(acc0);

  // Acc 1 -> Obj 0 (Block 1 EN)
  vm_accessor_t* acc1 = NULL;
  SE_ORIGIN_CALL(vm_accessor_create(&acc1, 1, 0, 0));
  (void)vm_accessor_cache_build(acc1);

  // Acc 2 -> Obj 1 (Block 2 EN - wired to Block 1's ENO for HARD INTERLOCK!)
  vm_accessor_t* acc2 = NULL;
  SE_ORIGIN_CALL(vm_accessor_create(&acc2, 2, 1, 0));
  (void)vm_accessor_cache_build(acc2);

  // Acc 3 -> Obj 0 (Block 3 EN - independent branch)
  vm_accessor_t* acc3 = NULL;
  SE_ORIGIN_CALL(vm_accessor_create(&acc3, 3, 0, 0));
  (void)vm_accessor_cache_build(acc3);

  // 4. Block 0: VM_BLK_TIMER (TON_INV, 500ms)
  vm_block_timer_data_t timer_data = {0};
  vm_block_timer_init_data(&timer_data, VM_TIMER_TON_INV, 500, false);
  vm_block_h blk0 = NULL;
  SE_ORIGIN_CALL(vm_block_create(
      &blk0, 0,
      &(vm_block_cfg_t){
          .block_idx = 0,
          .block_type = VM_BLK_TIMER,
          .in_cnt = 1,
          .q_cnt = 1,
          .en_cnt = 0,
          .eno_obj_id = VM_BLOCK_NO_ID,
          .custom_len = sizeof(timer_data),
          .custom_data = &timer_data,
          .in_acc_ids = (const uint16_t[]){0},
          .out_obj_ids = (const uint16_t[]){0},
      }));

  // 5. Block 1: VM_BLK_IO_TOGGLE (GPIO 7) - Publishes ENO to Object 1
  vm_block_io_toggle_data_t toggle_data_7 = {0};
  vm_block_io_toggle_init_data(&toggle_data_7, DEVICE_ID_GPIO_ESP, 7, (1ULL << 7));
  vm_block_h blk1 = NULL;
  SE_ORIGIN_CALL(vm_block_create(
      &blk1, 1,
      &(vm_block_cfg_t){
          .block_idx = 1,
          .block_type = VM_BLK_IO_TOGGLE,
          .in_cnt = 0,
          .q_cnt = 0,
          .en_cnt = 1,
          .en_mode = VM_BLK_EN_ALL,
          .eno_obj_id = 1,
          .custom_len = sizeof(toggle_data_7),
          .custom_data = &toggle_data_7,
          .en_acc_ids = (const uint16_t[]){1},
      }));

  // 6. Block 2: VM_BLK_IO_TOGGLE (GPIO 5) - Interlocked: EN wired to Acc 2 (Block 1 ENO)
  vm_block_io_toggle_data_t toggle_data_5 = {0};
  vm_block_io_toggle_init_data(&toggle_data_5, DEVICE_ID_GPIO_ESP, 5, (1ULL << 5));
  vm_block_h blk2 = NULL;
  SE_ORIGIN_CALL(vm_block_create(
      &blk2, 2,
      &(vm_block_cfg_t){
          .block_idx = 2,
          .block_type = VM_BLK_IO_TOGGLE,
          .in_cnt = 0,
          .q_cnt = 0,
          .en_cnt = 1,
          .en_mode = VM_BLK_EN_ALL,
          .eno_obj_id = VM_BLOCK_NO_ID,
          .custom_len = sizeof(toggle_data_5),
          .custom_data = &toggle_data_5,
          .en_acc_ids = (const uint16_t[]){2},
      }));

  // 7. Block 3: VM_BLK_IO_TOGGLE (GPIO 6)
  vm_block_io_toggle_data_t toggle_data_6 = {0};
  vm_block_io_toggle_init_data(&toggle_data_6, DEVICE_ID_GPIO_ESP, 6, (1ULL << 6));
  vm_block_h blk3 = NULL;
  SE_ORIGIN_CALL(vm_block_create(
      &blk3, 3,
      &(vm_block_cfg_t){
          .block_idx = 3,
          .block_type = VM_BLK_IO_TOGGLE,
          .in_cnt = 0,
          .q_cnt = 0,
          .en_cnt = 1,
          .en_mode = VM_BLK_EN_ALL,
          .eno_obj_id = VM_BLOCK_NO_ID,
          .custom_len = sizeof(toggle_data_6),
          .custom_data = &toggle_data_6,
          .en_acc_ids = (const uint16_t[]){3},
      }));

  // 8. Set VM execution mode to RUNNING so the supervisor task executes passes
  vm_exec_set_mode(VM_RUN_RUNNING);
  ESP_LOGI("runit_blink", "4-block VM 3-LED blink program running (Core 1 supervisor)");

  // 7. Start periodic scan cycle metrics reporter (1 Hz report)
  static TaskHandle_t s_metrics_task_h = NULL;
  if (s_metrics_task_h == NULL) {
    xTaskCreate(runit_scan_metrics_task, "scan_metrics", 4096, NULL, 1, &s_metrics_task_h);
  }

  return NULL;
}

#include "sys_device.h"

static void runit_scan_metrics_task(void* arg) {
  (void)arg;
  uint32_t last_passes = 0;
  uint32_t sec = 0;

  ESP_LOGI("safety_test", "========================================================================");
  ESP_LOGI("safety_test", ">>> STARTING RUN-IT HARD & CRITICAL CONTAINMENT TEST SUITE <<<");
  ESP_LOGI("safety_test", "Phase 1: Baseline Normal Run (t = 0..8s) - 3 LEDs in sync @ 100 Hz");
  ESP_LOGI("safety_test", "Phase 2: Downstream Interlock Hard Containment (t = 8..16s) - Block 1 ENO -> Block 2 interlocked");
  ESP_LOGI("safety_test", "Phase 3: Watchdog Hard Containment (t = 16..24s) - Hung block -> Core 0 timer catches it");
  ESP_LOGI("safety_test", "Phase 4: VM Domain Critical Trip (t = 24..32s) - sys_vm_handle_fault hard-stops VM");
  ESP_LOGI("safety_test", "Phase 5: Safe Recovery (t = 32s+) - Acknowledge, unfreeze, and resume 100 Hz");
  ESP_LOGI("safety_test", "========================================================================");

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    sec++;

    vm_exec_perf_t p = vm_exec_get_perf();
    uint32_t delta = p.pass_count - last_passes;
    last_passes = p.pass_count;
    vm_run_mode_e mode = vm_exec_mode();

    if (sec < 8) {
      // Phase 1: Baseline
      uint32_t avg_pass_us = (p.pass_count > 0) ? (uint32_t)(p.total_pass_us / p.pass_count) : 0;
      uint32_t avg_cycle_us = (p.pass_count > 1) ? (uint32_t)(p.total_cycle_us / (p.pass_count - 1)) : 0;
      ESP_LOGI("safety_test",
               "[PHASE 1 - BASELINE] t=%lus | Exec: %lu us (avg %lu) | Cycle: %lu us (avg %lu) | Rate: %lu scans/sec (3 LEDs blinking)",
               (unsigned long)sec, (unsigned long)p.last_pass_us, (unsigned long)avg_pass_us,
               (unsigned long)p.last_cycle_us, (unsigned long)avg_cycle_us, (unsigned long)delta);
    }
    else if (sec == 8) {
      // Phase 2 Transition: Downstream Interlock Hard Containment
      ESP_LOGW("safety_test", "========================================================================");
      ESP_LOGW("safety_test", ">>> PHASE 2: DOWNSTREAM INTERLOCK HARD CONTAINMENT (t = 8..16s) <<<");
      ESP_LOGW("safety_test", "Trigger: Block 1 mutates to unauthorized pin 15.");
      ESP_LOGW("safety_test", "Expected: Block 1 fails -> drops ENO=false -> Downstream Block 2 (GPIO 5) is HARD-INHIBITED!");
      ESP_LOGW("safety_test", "Both LED 7 and LED 5 stop. Independent LED 6 continues blinking!");
      ESP_LOGW("safety_test", "========================================================================");

      vm_block_h b1 = vm_block_get_by_id(1);
      if (b1) {
        vm_block_io_toggle_data_t* d1 = (vm_block_io_toggle_data_t*)vm_block_get_custom_data(b1);
        if (d1) d1->default_io_num = 15;
      }
    }
    else if (sec > 8 && sec < 16) {
      // Phase 2 Running
      ESP_LOGI("safety_test",
               "[PHASE 2 - INTERLOCKED] t=%lus | Rate: %lu scans/sec | LED 7 & 5 contained (OFF), LED 6 blinking",
               (unsigned long)sec, (unsigned long)delta);
    }
    else if (sec == 16) {
      // Phase 3 Transition: Watchdog Hard Containment
      ESP_LOGW("safety_test", "========================================================================");
      ESP_LOGW("safety_test", ">>> PHASE 3: BLOCK WATCHDOG HARD CONTAINMENT (t = 16..24s) <<<");
      ESP_LOGW("safety_test", "Trigger: Restore pin 7, but inject 25ms stall into Block 1 (limit 20ms).");
      ESP_LOGW("safety_test", "Expected: Core 0 timer catches runaway block, fires ERR_VM_EXEC_BLOCK_HUNG (block 1)!");
      ESP_LOGW("safety_test", "========================================================================");

      vm_block_h b1 = vm_block_get_by_id(1);
      if (b1) {
        vm_block_io_toggle_data_t* d1 = (vm_block_io_toggle_data_t*)vm_block_get_custom_data(b1);
        if (d1) {
          d1->default_io_num = 7;
          d1->flags |= VM_IO_TOGGLE_F_STALL_WD; // 25ms stall!
        }
      }
    }
    else if (sec > 16 && sec < 24) {
      // Phase 3 Running
      ESP_LOGW("safety_test",
               "[PHASE 3 - WATCHDOG MONITOR] t=%lus | Exec: %lu us (spiked by 25ms stall!) | Rate: %lu scans/sec",
               (unsigned long)sec, (unsigned long)p.last_pass_us, (unsigned long)delta);
    }
    else if (sec == 24) {
      // Phase 4 Transition: VM Domain Critical Fault Trip
      ESP_LOGE("safety_test", "========================================================================");
      ESP_LOGE("safety_test", ">>> PHASE 4: VM SUBSYSTEM CRITICAL TRIP (t = 24..32s) <<<");
      ESP_LOGE("safety_test", "Trigger: Clear stall, emit critical VM domain fault ERR_VM_EXEC_FAULT_LATCHED.");
      ESP_LOGE("safety_test", "Expected: sys_vm_handle_fault intercepts, hard-latches VM & stops execution!");
      ESP_LOGE("safety_test", "========================================================================");

      vm_block_h b1 = vm_block_get_by_id(1);
      if (b1) {
        vm_block_io_toggle_data_t* d1 = (vm_block_io_toggle_data_t*)vm_block_get_custom_data(b1);
        if (d1) d1->flags &= ~VM_IO_TOGGLE_F_STALL_WD;
      }

      // Inject critical VM domain error
      SE_push_to_handler(SE_ERR_NEW_OWNED(OWNER_VM_EXEC, ERR_VM_EXEC_FAULT_LATCHED,
                                          .device_id = 0, .root_tag = ERR_VM_EXEC_BLOCK_HUNG, .root_owner = OWNER_VM_EXEC));
    }
    else if (sec > 24 && sec < 32) {
      // Phase 4 Running: VM stopped
      vm_exec_fault_status_t fs = vm_exec_fault_status();
      ESP_LOGW("safety_test",
               "[PHASE 4 - VM HARD STOPPED] t=%lus | VM Mode: %u (0=STOPPED) | Latched: %s (dev=%u, owner=0x%04x, tag=%u, hits=%lu) | Rate: %lu scans/sec",
               (unsigned long)sec, (unsigned)mode, fs.latched ? "YES" : "NO",
               fs.device_id, (unsigned)fs.root_owner, (unsigned)fs.root_tag,
               (unsigned long)fs.occurrences, (unsigned long)delta);
    }
    else if (sec == 32) {
      // Phase 5 Transition: Complete Recovery
      ESP_LOGI("safety_test", "========================================================================");
      ESP_LOGI("safety_test", ">>> PHASE 5: COMPLETE SYSTEM RECOVERY <<<");
      ESP_LOGI("safety_test", "Acknowledging VM fault, syncing devices, resuming VM running mode");
      ESP_LOGI("safety_test", "========================================================================");

      err_h ack_err = vm_exec_fault_acknowledge();
      if (ack_err) {
        ESP_LOGE("safety_test", "Failed to acknowledge fault");
        SE_release(ack_err);
      } else {
        ESP_LOGI("safety_test", "vm_exec_fault_acknowledge: OK");
      }

      err_h sync_err = sys_device_sync_all();
      if (sync_err) {
        ESP_LOGE("safety_test", "Failed to sync devices");
        SE_release(sync_err);
      } else {
        ESP_LOGI("safety_test", "sys_device_sync_all: OK (devices unfreezed)");
      }

      vm_exec_set_mode(VM_RUN_RUNNING);
      ESP_LOGI("safety_test", "vm_exec_set_mode(VM_RUN_RUNNING): VM resumed!");
    }
    else {
      // Phase 5 Running: Steady state resumed
      uint32_t avg_pass_us = (p.pass_count > 0) ? (uint32_t)(p.total_pass_us / p.pass_count) : 0;
      uint32_t avg_cycle_us = (p.pass_count > 1) ? (uint32_t)(p.total_cycle_us / (p.pass_count - 1)) : 0;
      ESP_LOGI("safety_test",
               "[PHASE 5 - RECOVERED] t=%lus | Exec: %lu us (avg %lu) | Cycle: %lu us (avg %lu) | Rate: %lu scans/sec (All 3 LEDs blinking!)",
               (unsigned long)sec, (unsigned long)p.last_pass_us, (unsigned long)avg_pass_us,
               (unsigned long)p.last_cycle_us, (unsigned long)avg_cycle_us, (unsigned long)delta);
    }
  }
}


