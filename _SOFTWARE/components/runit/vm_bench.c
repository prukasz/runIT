#include "vm_bench.h"
#include <esp_cpu.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>
#include "vm_block.h"
#include "vm_obj_access.h"
#include "vm_obj_build.h"


#define OWNER OWNER_VM_BASE

static const char* TAG = "vm_bench";

#define GRID_N 8
#define CELLS (GRID_N * GRID_N)

/* One pass is CELLS accesses. REPS keeps a pass long enough that the timer
   read at each end is noise; TRIALS lets the best pass win, so an interrupt
   landing mid-run shows up as a discarded trial rather than as the number. */
#define REPS 500
#define TRIALS 3

static uint8_t s_pool[8192];
static vm_alloc_t s_arena;
static vm_obj_h s_slots[16];

static vm_obj_h s_grid;          // VM_OBJ_PTR[8] -- the 2D container
static vm_obj_h s_rows[GRID_N];  // VM_OBJ_F[8]   -- one per row
static vm_obj_h s_flat;          // VM_OBJ_F[64]  -- same data, one object
static vm_obj_h s_rsel, s_csel;  // live row/column selectors for by-ref

static vm_accessor_t* s_flat_acc[CELLS];  // flat[i]
static vm_accessor_t* s_grid_acc[CELLS];  // grid[r][c], both literal
static vm_accessor_t* s_name_acc[CELLS];  // grid["rN"][c]
static vm_accessor_t* s_row_acc[GRID_N];  // grid[r], resolved to the row object
static vm_accessor_t* s_ref_acc;          // grid[rsel][csel], both by-ref
static uint8_t s_add_blk_buf[160];
static uint8_t s_blk_arr_buf[160];
static uint8_t s_blk_tag_buf[160];
static vm_block_h s_blk_scalar;
static vm_block_h s_blk_array;
static vm_block_h s_blk_tag;


// keeps the optimiser from deleting a scenario whose result nobody reads
static volatile float s_sink;

static uint32_t s_cpu_mhz = 240;

#define OBJ_GRID 0
#define OBJ_ROW0 1  // rows occupy 1..8
#define OBJ_FLAT 9
#define OBJ_RSEL 10
#define OBJ_CSEL 11

/* ==========================================================================
   Fixtures
   ========================================================================== */

static bool s_ok;
#define OKC(call)                 \
  do {                            \
    if (s_ok && (call) != NULL) { \
      s_ok = false;               \
    }                             \
  } while (0)

static err_h mk(vm_obj_h* out, uint16_t id, vm_obj_t_e type, uint16_t items, const char* name) {
  SE_RET_IF_ERR(vm_obj_create(out, &s_arena, &(vm_obj_cfg_t){.type = type, .item_count = items, .name = name, .mutable = true}));
  SE_RET_IF_ERR(vm_obj_table_set(&g_vm_obj_table, id, *out));
  return NULL;
}

static bool setup(void) {
  vm_alloc_init(&s_arena, s_pool, sizeof(s_pool));
  memset(s_slots, 0, sizeof(s_slots));
  g_vm_obj_table.items = s_slots;
  g_vm_obj_table.count = 16;
  s_ok = true;

  char rowname[GRID_N][3];
  for (int r = 0; r < GRID_N; r++) {
    rowname[r][0] = 'r';
    rowname[r][1] = (char)('0' + r);
    rowname[r][2] = '\0';
  }

  OKC(mk(&s_grid, OBJ_GRID, VM_OBJ_PTR, GRID_N, NULL));
  for (int r = 0; r < GRID_N; r++) {
    // tagged so the by-name scenario has something to match
    OKC(mk(&s_rows[r], (uint16_t)(OBJ_ROW0 + r), VM_OBJ_F, GRID_N, rowname[r]));
    OKC(vm_obj_link_direct(s_grid, (uint16_t)r, s_rows[r]));
  }
  OKC(mk(&s_flat, OBJ_FLAT, VM_OBJ_F, CELLS, NULL));
  OKC(mk(&s_rsel, OBJ_RSEL, VM_OBJ_U8, 1, NULL));
  OKC(mk(&s_csel, OBJ_CSEL, VM_OBJ_U8, 1, NULL));
  if (!s_ok) return false;

  // cell (r,c) holds r*8+c, so every full traversal sums to 2016
  for (int r = 0; r < GRID_N; r++) {
    float* row = (float*)s_rows[r]->payload;
    for (int c = 0; c < GRID_N; c++) row[c] = (float)(r * GRID_N + c);
  }
  for (int i = 0; i < CELLS; i++) ((float*)s_flat->payload)[i] = (float)i;

  for (int r = 0; r < GRID_N; r++) {
    for (int c = 0; c < GRID_N; c++) {
      int i = r * GRID_N + c;

      OKC(vm_accessor_create(&s_grid_acc[i], &s_arena, OBJ_GRID, 2));
      OKC(vm_accessor_set_literal(s_grid_acc[i], 0, (uint32_t)r));
      OKC(vm_accessor_set_literal(s_grid_acc[i], 1, (uint32_t)c));

      OKC(vm_accessor_create(&s_name_acc[i], &s_arena, OBJ_GRID, 2));
      OKC(vm_accessor_set_name(s_name_acc[i], 0, &s_arena, rowname[r], 2));
      OKC(vm_accessor_set_literal(s_name_acc[i], 1, (uint32_t)c));

      OKC(vm_accessor_create(&s_flat_acc[i], &s_arena, OBJ_FLAT, 1));
      OKC(vm_accessor_set_literal(s_flat_acc[i], 0, (uint32_t)i));
    }
    OKC(vm_accessor_create(&s_row_acc[r], &s_arena, OBJ_GRID, 1));
    OKC(vm_accessor_set_literal(s_row_acc[r], 0, (uint32_t)r));
  }

  /* The by-ref chain: two whole-object accessors feeding the index slots, so
     each access re-reads both selectors -- the sequencer / mux shape. */
  vm_accessor_t* rsel_acc = NULL;
  vm_accessor_t* csel_acc = NULL;
  OKC(vm_accessor_create(&rsel_acc, &s_arena, OBJ_RSEL, 0));
  OKC(vm_accessor_create(&csel_acc, &s_arena, OBJ_CSEL, 0));
  OKC(vm_accessor_create(&s_ref_acc, &s_arena, OBJ_GRID, 2));
  OKC(vm_accessor_set_ref(s_ref_acc, 0, rsel_acc));
  OKC(vm_accessor_set_ref(s_ref_acc, 1, csel_acc));

  // 3 Block Mockup Fixtures:
  // 1. Scalar block: inputs are 0-index scalar accessors (flat_acc[0], flat_acc[1])
  memset(s_add_blk_buf, 0, sizeof(s_add_blk_buf));
  s_blk_scalar = (vm_block_h)s_add_blk_buf;
  s_blk_scalar->cfg.block_idx = 1;
  s_blk_scalar->cfg.block_type = 1;
  s_blk_scalar->cfg.in_cnt = 2;
  s_blk_scalar->cfg.q_cnt = 1;
  vm_block_inputs(s_blk_scalar)[0] = s_flat_acc[0];
  vm_block_inputs(s_blk_scalar)[1] = s_flat_acc[1];
  vm_block_outputs(s_blk_scalar)[0] = s_flat;

  // 2. 1D Array block: inputs index into array elements (s_grid_acc[2], s_grid_acc[3])
  memset(s_blk_arr_buf, 0, sizeof(s_blk_arr_buf));
  s_blk_array = (vm_block_h)s_blk_arr_buf;
  s_blk_array->cfg.block_idx = 2;
  s_blk_array->cfg.block_type = 1;
  s_blk_array->cfg.in_cnt = 2;
  s_blk_array->cfg.q_cnt = 1;
  vm_block_inputs(s_blk_array)[0] = s_grid_acc[2];
  vm_block_inputs(s_blk_array)[1] = s_grid_acc[3];
  vm_block_outputs(s_blk_array)[0] = s_flat;

  // 3. Tag accessor block: inputs use tag matching ("r0"[1], "r1"[1])
  memset(s_blk_tag_buf, 0, sizeof(s_blk_tag_buf));
  s_blk_tag = (vm_block_h)s_blk_tag_buf;
  s_blk_tag->cfg.block_idx = 3;
  s_blk_tag->cfg.block_type = 1;
  s_blk_tag->cfg.in_cnt = 2;
  s_blk_tag->cfg.q_cnt = 1;
  vm_block_inputs(s_blk_tag)[0] = s_name_acc[1];
  vm_block_inputs(s_blk_tag)[1] = s_name_acc[9];
  vm_block_outputs(s_blk_tag)[0] = s_flat;

  return s_ok;
}

/* ==========================================================================
   Scenarios -- each performs exactly CELLS accesses and returns their sum
   ========================================================================== */

// the floor: what walking the same bytes costs with no VM in the way
static float bench_raw(void) {
  float acc = 0;
  for (int r = 0; r < GRID_N; r++) {
    const float* row = (const float*)s_rows[r]->payload;
    for (int c = 0; c < GRID_N; c++) acc += row[c];
  }
  return acc;
}

// one object, one literal index -- the cheapest accessor there is
static float bench_flat_literal(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    float v = 0;
    if (VM_OBJ_GET_VAL(v, s_flat_acc[i]) == NULL) acc += v;
  }
  return acc;
}

// the 2D shape: same read, one VM_OBJ_PTR dereference deeper
static float bench_grid_literal(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    float v = 0;
    if (VM_OBJ_GET_VAL(v, s_grid_acc[i]) == NULL) acc += v;
  }
  return acc;
}

/* Both indices read live from other objects. The selectors are written
   directly rather than through VM_OBJ_SET_VAL so the figure stays a pure
   read cost. */
static float bench_grid_ref(void) {
  float acc = 0;
  for (int r = 0; r < GRID_N; r++) {
    *(uint8_t*)s_rsel->payload = (uint8_t)r;
    for (int c = 0; c < GRID_N; c++) {
      *(uint8_t*)s_csel->payload = (uint8_t)c;
      float v = 0;
      if (VM_OBJ_GET_VAL(v, s_ref_acc) == NULL) acc += v;
    }
  }
  return acc;
}

// the message shape: a tag scan replaces the first literal. Averaged over all
// eight rows, so it covers both a first-slot and a last-slot match.
static float bench_grid_name(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    float v = 0;
    if (VM_OBJ_GET_VAL(v, s_name_acc[i]) == NULL) acc += v;
  }
  return acc;
}

/* The fold-block shape: resolve the row once, then step its payload. Same 64
   values, but eight resolves instead of sixty-four. */
static float bench_row_payload(void) {
  float acc = 0;
  for (int r = 0; r < GRID_N; r++) {
    vm_obj_h row = NULL;
    if (vm_get_obj(&row, s_row_acc[r]) != NULL) continue;
    vm_payload_t p = vm_obj_as_payload(row);
    for (uint16_t i = 0; i < p.count; i++) {
      float v = 0;
      VM_PAYLOAD_GET_VAL(v, vm_payload_at(p, i));
      acc += v;
    }
  }
  return acc;
}

// the write path over the same chain -- adds the mutable check and the upd flag
static float bench_grid_write(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    float v = (float)i;
    if (VM_OBJ_SET_VAL(v, s_grid_acc[i]) == NULL) acc += 1.0f;
  }
  return acc;
}

static void block_add_execute(vm_block_h block) {
  IF_BLOCK_ENABLED(block) {
    const vm_accessor_t *in0 = NULL, *in1 = NULL;
    vm_obj_h out0 = NULL;
    BLOCK_CALL(vm_block_get_in(&in0, block, 0), block);
    BLOCK_CALL(vm_block_get_in(&in1, block, 1), block);
    BLOCK_CALL(vm_block_get_out(&out0, block, 0), block);
    float a = 0, b = 0;
    BLOCK_CALL(VM_OBJ_GET_VAL(a, in0), block);
    BLOCK_CALL(VM_OBJ_GET_VAL(b, in1), block);
    float sum = a + b;
    BLOCK_CALL(VM_OBJ_SET_VAL_AT(sum, out0, 0), block);
    vm_block_set_ENO(block, true);
  } else {
    vm_block_set_ENO(block, false);
  }
}

static float bench_block_scalar(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    block_add_execute(s_blk_scalar);
    acc += ((float*)s_flat->payload)[0];
  }
  return acc;
}

static float bench_block_array(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    block_add_execute(s_blk_array);
    acc += ((float*)s_flat->payload)[0];
  }
  return acc;
}

static float bench_block_tag(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    block_add_execute(s_blk_tag);
    acc += ((float*)s_flat->payload)[0];
  }
  return acc;
}



/* ==========================================================================
   Harness
   ========================================================================== */

typedef float (*bench_fn_t)(void);

static uint32_t s_raw_cyc_x10;

static void measure_cpu_mhz(void) {
  int64_t t0 = esp_timer_get_time();
  uint32_t c0 = esp_cpu_get_cycle_count();
  while (esp_timer_get_time() - t0 < 20000) {
  }
  uint32_t c1 = esp_cpu_get_cycle_count();
  int64_t t1 = esp_timer_get_time();
  uint32_t us = (uint32_t)(t1 - t0);
  if (us) s_cpu_mhz = (c1 - c0) / us;
  if (!s_cpu_mhz) s_cpu_mhz = 240;
}

static void run(const char* name, bench_fn_t fn, float expect) {
  float got = fn();  // warm the icache, and prove the scenario actually works
  bool correct = (got > expect - 0.5f) && (got < expect + 0.5f);

  uint32_t best = UINT32_MAX;
  for (int t = 0; t < TRIALS; t++) {
    float sum = 0;
    uint32_t c0 = esp_cpu_get_cycle_count();
    for (int r = 0; r < REPS; r++) sum += fn();
    uint32_t c1 = esp_cpu_get_cycle_count();
    s_sink += sum;
    uint32_t d = c1 - c0;
    if (d < best) best = d;
  }

  uint32_t accesses = (uint32_t)REPS * CELLS;
  uint32_t cyc_x10 = (uint32_t)(((uint64_t)best * 10u) / accesses);
  uint32_t ns_x10 = (uint32_t)(((uint64_t)best * 10000u) / ((uint64_t)accesses * s_cpu_mhz));
  if (!s_raw_cyc_x10) s_raw_cyc_x10 = cyc_x10 ? cyc_x10 : 1;
  uint32_t rel_x10 = (uint32_t)(((uint64_t)cyc_x10 * 10u) / s_raw_cyc_x10);

  ESP_LOGI(TAG, "  %-36s %5lu.%lu %8lu.%lu %7lu.%lux%s", name, (unsigned long)(cyc_x10 / 10), (unsigned long)(cyc_x10 % 10), (unsigned long)(ns_x10 / 10), (unsigned long)(ns_x10 % 10), (unsigned long)(rel_x10 / 10), (unsigned long)(rel_x10 % 10), correct ? "" : "   <-- WRONG RESULT");
}

/* ==========================================================================
   Register-window probe

   Xtensa rotates the register file by 8 on every call8. When it wraps, the
   CPU takes a window-overflow exception to spill registers to the stack, and
   a matching underflow on the way back -- so a call chain that happens to sit
   on that boundary pays for both on every single call.

   Windows repeat with period 8, so running the identical workload from eight
   different starting depths separates the two possibilities cleanly: a
   sawtooth means the resolve path is paying window exceptions and the fix is
   to flatten the call chain; a flat line means the cost is the code itself
   and the call structure is irrelevant.
   ========================================================================== */

static volatile float s_depth_guard;

static float __attribute__((noinline)) depth_trampoline(int n, bench_fn_t fn) {
  if (n <= 0) return fn();
  float v = depth_trampoline(n - 1, fn);
  /* The store has to happen *after* the call, or GCC turns this into a tail
     call -- a jump, which does not rotate the window and would make the whole
     probe measure nothing. */
  s_depth_guard = v;
  return v;
}

static void run_depth_sweep(const char* name, bench_fn_t fn) {
  ESP_LOGI(TAG, "  window probe -- %s, same work from 8 call depths:", name);
  for (int d = 0; d < 8; d++) {
    uint32_t best = UINT32_MAX;
    for (int t = 0; t < TRIALS; t++) {
      float sum = 0;
      uint32_t c0 = esp_cpu_get_cycle_count();
      for (int r = 0; r < REPS; r++) sum += depth_trampoline(d, fn);
      uint32_t c1 = esp_cpu_get_cycle_count();
      s_sink += sum;
      uint32_t dd = c1 - c0;
      if (dd < best) best = dd;
    }
    uint32_t cyc_x10 = (uint32_t)(((uint64_t)best * 10u) / ((uint32_t)REPS * CELLS));
    ESP_LOGI(TAG, "    +%d frames   %5lu.%lu cyc/acc", d, (unsigned long)(cyc_x10 / 10), (unsigned long)(cyc_x10 % 10));
  }
}

void vm_bench_run(void) {
  ESP_LOGW(TAG, "==== VM object access benchmark ====");

  if (!setup()) {
    ESP_LOGE(TAG, "fixture setup failed -- arena too small or an id clashed");
    return;
  }
  measure_cpu_mhz();

  ESP_LOGI(TAG, "%lu MHz CPU, %dx%d float grid (%d cells), %d reps x %d trials, best trial kept", (unsigned long)s_cpu_mhz, GRID_N, GRID_N, CELLS, REPS, TRIALS);
  ESP_LOGI(TAG, "arena %lu/%u B", (unsigned long)vm_alloc_used(&s_arena), (unsigned)sizeof(s_pool));
  ESP_LOGI(TAG, "  %-36s %7s %10s %9s", "scenario", "cyc/acc", "ns/acc", "vs raw");

  // raw first: every other row is reported as a multiple of it
  run("raw C pointer walk", bench_raw, 2016.0f);
  run("flat[i]            1 literal", bench_flat_literal, 2016.0f);
  run("grid[r][c]         2 literal", bench_grid_literal, 2016.0f);
  run("grid[rsel][csel]   2 by-ref", bench_grid_ref, 2016.0f);
  run("grid[\"rN\"][c]      name + literal", bench_grid_name, 2016.0f);
  run("row resolved once, payload walk", bench_row_payload, 2016.0f);
  run("grid[r][c] WRITE   2 literal", bench_grid_write, (float)CELLS);
  run("block_add (scalar in/out)", bench_block_scalar, 2080.0f);
  run("block_add (1D array arr[2]+arr[3])", bench_block_array, 320.0f);
  run("block_add (tag \"r0\"+\"r1\")", bench_block_tag, 640.0f);





  /* Probe the cheapest chained scenario: it has the fewest moving parts, so
     anything periodic in it is the call structure and not the work. */
  run_depth_sweep("flat[i] 1 literal", bench_flat_literal);

  ESP_LOGW(TAG, "==== benchmark done ====");
}
