#include "vm_stats.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "vm_clock.h"
#include "vm_store.h"

static const char* TAG = "vm_exec";  // the log lines keep the supervisor's tag

#define VM_PROFILE_MAX_BLOCKS 64
#define VM_PROFILE_TOP 4

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t s_pass_cnt;
static uint32_t s_last_pass_us;
static uint32_t s_pass_us_min = UINT32_MAX;
static uint32_t s_pass_us_max;
static uint64_t s_pass_us_sum;

static uint64_t s_last_pass_start_us;
static uint32_t s_cycle_us_last;
static uint32_t s_cycle_us_min = UINT32_MAX;
static uint32_t s_cycle_us_max;
static uint64_t s_cycle_us_sum;

uint32_t vm_exec_pass_count(void) {
  return s_pass_cnt;
}

uint32_t vm_exec_last_pass_us(void) {
  return s_last_pass_us;
}

vm_exec_perf_t vm_exec_get_perf(void) {
  portENTER_CRITICAL(&s_mux);
  vm_exec_perf_t p = {
      .pass_count     = s_pass_cnt,
      .last_pass_us   = s_last_pass_us,
      .min_pass_us    = (s_pass_us_min == UINT32_MAX) ? 0 : s_pass_us_min,
      .max_pass_us    = s_pass_us_max,
      .total_pass_us  = s_pass_us_sum,
      .last_cycle_us  = s_cycle_us_last,
      .min_cycle_us   = (s_cycle_us_min == UINT32_MAX) ? 0 : s_cycle_us_min,
      .max_cycle_us   = s_cycle_us_max,
      .total_cycle_us = s_cycle_us_sum,
  };
  portEXIT_CRITICAL(&s_mux);
  return p;
}

void vm_exec_reset_stats(void) {
  portENTER_CRITICAL(&s_mux);
  s_pass_cnt           = 0;
  s_last_pass_us       = 0;
  s_pass_us_min        = UINT32_MAX;
  s_pass_us_max        = 0;
  s_pass_us_sum        = 0;
  s_last_pass_start_us = 0;
  s_cycle_us_last      = 0;
  s_cycle_us_min       = UINT32_MAX;
  s_cycle_us_max       = 0;
  s_cycle_us_sum       = 0;
  portEXIT_CRITICAL(&s_mux);
}

void vm_stats_pass_begin(uint64_t t0_us) {
  portENTER_CRITICAL(&s_mux);
  if (s_last_pass_start_us != 0) {
    const uint32_t cycle = (uint32_t)(t0_us - s_last_pass_start_us);
    s_cycle_us_last = cycle;
    if (cycle < s_cycle_us_min) s_cycle_us_min = cycle;
    if (cycle > s_cycle_us_max) s_cycle_us_max = cycle;
    s_cycle_us_sum += cycle;
  }
  s_last_pass_start_us = t0_us;
  portEXIT_CRITICAL(&s_mux);
}

#if VM_STATS_PROFILE
static uint32_t s_ph_sum[VM_PH_COUNT], s_ph_max[VM_PH_COUNT];
static uint32_t s_blocks_min = UINT32_MAX;
static uint32_t s_blk_sum[VM_PROFILE_MAX_BLOCKS], s_blk_max[VM_PROFILE_MAX_BLOCKS], s_blk_runs[VM_PROFILE_MAX_BLOCKS];
static uint8_t s_blk_type[VM_PROFILE_MAX_BLOCKS];

void vm_stats_phase(vm_phase_e phase, uint32_t us) {
  s_ph_sum[phase] += us;
  if (us > s_ph_max[phase]) s_ph_max[phase] = us;
  if (phase == VM_PH_BLOCKS && us < s_blocks_min) s_blocks_min = us;
}

void vm_stats_block(uint16_t idx, uint8_t type, uint32_t us) {
  if (idx >= VM_PROFILE_MAX_BLOCKS) return;
  s_blk_sum[idx] += us;
  if (us > s_blk_max[idx]) s_blk_max[idx] = us;
  s_blk_runs[idx]++;
  s_blk_type[idx] = type;
}

/* The slowest blocks by total time in the window, as "#index(type) avg/max us" (a FOR includes its body). */
static void log_profile(uint32_t window_passes, uint32_t window_max, uint64_t window_sum) {
  /* Two lines to read: the blocks alone, then the whole pass (events, blocks, telemetry sample, retention). */
  ESP_LOGI(TAG, "blocks us: min %u max %u avg %u", (unsigned)s_blocks_min, (unsigned)s_ph_max[VM_PH_BLOCKS],
           (unsigned)(s_ph_sum[VM_PH_BLOCKS] / window_passes));
  ESP_LOGI(TAG, "pass us: max %u avg %u (telemetry max %u), %u passes, cycle %u", (unsigned)window_max,
           (unsigned)(window_sum / window_passes), (unsigned)s_ph_max[VM_PH_SAMPLE], (unsigned)window_passes, (unsigned)s_cycle_us_last);

  char top[VM_PROFILE_TOP * 34 + 1];
  size_t used = 0;
  bool taken[VM_PROFILE_MAX_BLOCKS] = {0};
  const uint16_t cnt = g_vm_store.reg[VM_REG_BLK].count;
  const uint16_t block_cnt = cnt < VM_PROFILE_MAX_BLOCKS ? cnt : VM_PROFILE_MAX_BLOCKS;
  top[0] = '\0';
  for (int rank = 0; rank < VM_PROFILE_TOP; rank++) {
    int best = -1;
    for (int i = 0; i < block_cnt; i++) {
      if (!taken[i] && s_blk_runs[i] && (best < 0 || s_blk_sum[i] > s_blk_sum[best])) best = i;
    }
    if (best < 0) break;
    taken[best] = true;
    used += (size_t)snprintf(top + used, sizeof(top) - used, "%s#%d(t%u) %u/%u", rank ? ", " : "", best, (unsigned)s_blk_type[best],
                             (unsigned)(s_blk_sum[best] / s_blk_runs[best]), (unsigned)s_blk_max[best]);
    if (used >= sizeof(top)) break;
  }
  ESP_LOGI(TAG, "slowest avg/max: %s", top);

  memset(s_ph_sum, 0, sizeof(s_ph_sum));
  memset(s_ph_max, 0, sizeof(s_ph_max));
  s_blocks_min = UINT32_MAX;
  memset(s_blk_sum, 0, sizeof(s_blk_sum));
  memset(s_blk_max, 0, sizeof(s_blk_max));
  memset(s_blk_runs, 0, sizeof(s_blk_runs));
}
#endif

void vm_stats_pass_end(bool completed, uint32_t dur_us) {
  if (!completed) return;

  portENTER_CRITICAL(&s_mux);
  s_last_pass_us = dur_us;
  if (dur_us < s_pass_us_min) s_pass_us_min = dur_us;
  if (dur_us > s_pass_us_max) s_pass_us_max = dur_us;
  s_pass_us_sum += dur_us;
  s_pass_cnt++;
  portEXIT_CRITICAL(&s_mux);

#if VM_PASS_TIME_LOG_MS
  /* One pass runs at a time, so the window needs no lock. Logged outside the critical section. */
  static uint64_t window_start_us;
  static uint32_t window_passes, window_min = UINT32_MAX, window_max;
  static uint64_t window_sum;
  const uint64_t now_us = vm_clock_us();
  if (window_start_us == 0) window_start_us = now_us - dur_us;
  window_passes++;
  window_sum += dur_us;
  if (dur_us < window_min) window_min = dur_us;
  if (dur_us > window_max) window_max = dur_us;
  if (now_us - window_start_us >= (uint64_t)VM_PASS_TIME_LOG_MS * 1000u) {
#if VM_STATS_PROFILE
    log_profile(window_passes, window_max, window_sum);
#else
    ESP_LOGI(TAG, "pass us: min %u max %u avg %u, %u passes, cycle %u", (unsigned)window_min, (unsigned)window_max,
             (unsigned)(window_sum / window_passes), (unsigned)window_passes, (unsigned)s_cycle_us_last);
#endif
    window_start_us = now_us;
    window_passes = 0;
    window_sum = 0;
    window_min = UINT32_MAX;
    window_max = 0;
  }
#endif
}
