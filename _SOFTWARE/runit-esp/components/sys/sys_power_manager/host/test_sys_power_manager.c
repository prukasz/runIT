/* Host unit tests of sys_power_manager (sys_power_manager.c + router) with real sys_device, sys_event, router and
 * an in-test mock power device implementing IO, PMON, VREG and PD contracts.
 * Run: python runit-esp/test_host/run.py sys_power_manager */
#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host_assert.h"
#include "sys_device.h"
#include "sys_device_driver.h"
#include "sys_io.h"
#include "sys_power_manager.h"
#include "sys_power_monitor.h"
#include "sys_power_usb_pd.h"
#include "sys_power_vreg.h"
#include "unity.h"

#define DEV_BUS   1u
#define DEV_RAIL1 2u
#define DEV_RAIL2 3u

/* ---- Fake multi-contract power device (IO + PMON + VREG + PD) ---- */

SYS_DEVICE_TYPE_DECLARE(PMMOCK, 0x0072)

typedef struct __attribute__((packed)) {
  uint8_t dummy;
} pmmock_cfg_t;

typedef struct {
  uint8_t id;
  uint8_t pin_level[4];
  int32_t ch_mV[2];
  int32_t ch_mA[2];
  uint8_t vreg_en;
  uint32_t vreg_mV;
  uint32_t vreg_mA;
  pd_settings_args_t pd;
  int resets;
} pmmock_t;

#define SYS_DEVICE_CTX pmmock_t

static pmmock_t* s_mock[SYS_DEVICE_COUNT];

static const sys_power_usb_pd_option_t k_pd_opts[] = {
    {.slot = 1, .type = SYS_POWER_USB_PD_OPTION_FIXED, .min_mV = 5000, .max_mV = 5000, .max_mA = 3000},
    {.slot = 2, .type = SYS_POWER_USB_PD_OPTION_FIXED, .min_mV = 9000, .max_mV = 9000, .max_mA = 3000},
    {.slot = 3, .type = SYS_POWER_USB_PD_OPTION_FIXED, .min_mV = 15000, .max_mV = 15000, .max_mA = 3000},
};

OP_DECLARE_READ(CONTRACT_IO, OP_LEVEL) {
  return CMD_REPLY(CONTRACT_IO, OP_LEVEL, .pin = args->pin, .level = c->pin_level[args->pin & 3u]);
}

OP_DECLARE_READ(CONTRACT_PMON, OP_VOLTAGE) {
  return CMD_REPLY(CONTRACT_PMON, OP_VOLTAGE, c->ch_mV[args->channel & 1u]);
}

OP_DECLARE_READ(CONTRACT_PMON, OP_CURRENT) {
  return CMD_REPLY(CONTRACT_PMON, OP_CURRENT, c->ch_mA[args->channel & 1u]);
}

OP_DECLARE_WRITE(CONTRACT_PMON, OP_ALERT) {
  (void)c;
  (void)args;
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_VREG, OP_ENABLE) {
  c->vreg_en = args->enable;
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_VREG, OP_VOLTAGE) {
  c->vreg_mV = args->voltage_mV;
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_VREG, OP_CURRENT) {
  c->vreg_mA = args->current_mA;
  return NULL;
}

OP_DECLARE_READ(CONTRACT_PD, OP_OPTIONS) {
  return cmd_reply(out, k_pd_opts, sizeof(k_pd_opts));
}

OP_DECLARE_WRITE(CONTRACT_PD, OP_SETTINGS) {
  c->pd = *args;
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  c->resets++;
  c->vreg_en = 0;
  c->vreg_mV = 5000;
  c->vreg_mA = 1000;
  return NULL;
}

static err_h pmmock_create(uint8_t id, pmmock_t* c, const pmmock_cfg_t* cfg) {
  (void)cfg;
  c->id = id;
  c->ch_mV[0] = 12000;
  c->vreg_mV = 5000;
  c->vreg_mA = 1000;
  s_mock[id] = c;
  return NULL;
}

static err_h pmmock_suspend(pmmock_t* c) {
  c->vreg_en = 0;
  return NULL;
}

static err_h pmmock_resume(pmmock_t* c) {
  (void)c;
  return NULL;
}

static const sys_device_op_t k_pmmock_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),
    OP_ENTRY_R(CONTRACT_IO, OP_LEVEL),
    OP_ENTRY_R(CONTRACT_PMON, OP_VOLTAGE),
    OP_ENTRY_R(CONTRACT_PMON, OP_CURRENT),
    OP_ENTRY_W(CONTRACT_PMON, OP_ALERT),
    OP_ENTRY_W(CONTRACT_VREG, OP_ENABLE),
    OP_ENTRY_W(CONTRACT_VREG, OP_VOLTAGE),
    OP_ENTRY_W(CONTRACT_VREG, OP_CURRENT),
    OP_ENTRY_R(CONTRACT_PD, OP_OPTIONS),
    OP_ENTRY_W(CONTRACT_PD, OP_SETTINGS),
};

SYS_DEVICE_TYPE_DEFINE(PMMOCK, pmmock_cfg_t, pmmock_create, pmmock_suspend, pmmock_resume, k_pmmock_ops)

/* ---- Event recorder & safe state hook ---- */

static atomic_int s_ev_limit;
static atomic_int s_ev_bat_low;
static atomic_int s_ev_bat_crit;
static atomic_int s_ev_pd_fixed;
static atomic_int s_ev_src_changed;
static atomic_int s_ev_input_ovp;
static atomic_int s_ev_budget_exc;
static atomic_int s_safe_state_calls;

static err_h on_pwrm_ev(const sys_event_t* ev, void* ctx) {
  (void)ctx;
  if (ev->key.tag == PWRM_EV_LIMIT_REACHED) atomic_fetch_add(&s_ev_limit, 1);
  else if (ev->key.tag == PWRM_EV_BATTERY_LOW) atomic_fetch_add(&s_ev_bat_low, 1);
  else if (ev->key.tag == PWRM_EV_BATTERY_CRITICAL) atomic_fetch_add(&s_ev_bat_crit, 1);
  else if (ev->key.tag == PWRM_EV_PD_FIXED) atomic_fetch_add(&s_ev_pd_fixed, 1);
  else if (ev->key.tag == PWRM_EV_SOURCE_CHANGED) atomic_fetch_add(&s_ev_src_changed, 1);
  else if (ev->key.tag == PWRM_EV_INPUT_OVP) atomic_fetch_add(&s_ev_input_ovp, 1);
  else if (ev->key.tag == PWRM_EV_BUDGET_EXCEEDED) atomic_fetch_add(&s_ev_budget_exc, 1);
  return NULL;
}

static void on_safe_state(void) {
  atomic_fetch_add(&s_safe_state_calls, 1);
}

static void create_mock(uint8_t id) {
  uint8_t frame[SYS_DEVICE_CREATE_HDR + sizeof(pmmock_cfg_t)] = {0};
  const uint16_t type = PMMOCK_TYPE;
  memcpy(frame, &type, sizeof(type));
  TEST_ASSERT_NULL(sys_device_write(id, CONTRACT_SYS_DEVICE_OP_CREATE, frame, sizeof(frame)));
}

static bool is_suspended(uint8_t id) {
  sys_device_info_t info = {0};
  TEST_ASSERT_NULL(SYS_DEVICE_GET(id, CONTRACT_SYS_DEVICE, OP_INFO, &info));
  return info.suspended != 0;
}

static sys_power_layout_t base_layout(void) {
  return (sys_power_layout_t){
      .max_mV = 10000,
      .max_mA = 5000,
      .reserve_mW = 2000,
      .unknown_source_mA = 1000,
      .vreg_efficiency_pct = 100,
      .input_device = 0xFFFFu,
      .input_channel = 0,
      .usb_pd = 0xFFFFu,
      .usb_ok = SYS_IO_PIN_NONE,
      .ext_ok = SYS_IO_PIN_NONE,
  };
}

void setUp(void) {
  atomic_store(&s_ev_limit, 0);
  atomic_store(&s_ev_bat_low, 0);
  atomic_store(&s_ev_bat_crit, 0);
  atomic_store(&s_ev_pd_fixed, 0);
  atomic_store(&s_ev_src_changed, 0);
  atomic_store(&s_ev_input_ovp, 0);
  atomic_store(&s_ev_budget_exc, 0);
  atomic_store(&s_safe_state_calls, 0);
  memset(s_mock, 0, sizeof(s_mock));
}

void tearDown(void) {
  TEST_ASSERT_NULL(sys_power_manager_stop());
  err_free(sys_device_sweep(CONTRACT_SYS_DEVICE_OP_REMOVE));
}

/* ---- Tests ---- */

static void test_stopped_until_layout_auto_start_and_stop(void) {
  sys_power_status_t st;
  err_h e = sys_power_manager_get_status(&st);
  TEST_ASSERT_ERR_TAG(ERR_PWRM_NOT_READY, e);
  err_free(e);

  const sys_power_layout_t lay = base_layout();
  TEST_ASSERT_NULL(sys_power_manager_set_layout(&lay));
  TEST_ASSERT_NULL(sys_power_manager_get_status(&st));
  TEST_ASSERT_EQUAL_UINT32(10000, st.budget_mW);
  TEST_ASSERT_EQUAL_UINT32(10000, st.limit_mW);

  TEST_ASSERT_NULL(sys_power_manager_stop());
  e = sys_power_manager_get_status(&st);
  TEST_ASSERT_ERR_TAG(ERR_PWRM_NOT_READY, e);
  err_free(e);
}

static void test_runtime_rail_add_remove_and_auto_remove(void) {
  create_mock(DEV_RAIL1);
  const sys_power_layout_t lay = base_layout();
  TEST_ASSERT_NULL(sys_power_manager_set_layout(&lay));

  sys_power_response_row_t rows[4];
  uint8_t count = 0;
  TEST_ASSERT_NULL(sys_power_manager_get_matrix(rows, 4, &count));
  TEST_ASSERT_EQUAL_UINT8(1, count);

  const sys_power_rail_t r = {.vreg = DEV_RAIL1, .monitor_device = 0xFFFFu, .min_mA = 200};
  TEST_ASSERT_NULL(sys_power_manager_add_rail(&r));
  /* Duplicate rejected */
  err_h dup = sys_power_manager_add_rail(&r);
  TEST_ASSERT_ERR_TAG(ERR_PWRM_RAIL_TAKEN, dup);
  TEST_ASSERT_EQUAL_UINT16(DEV_RAIL1, ERR_DATA(dup, ERR_PWRM_RAIL_TAKEN)->id);
  err_free(dup);

  TEST_ASSERT_NULL(sys_power_manager_get_matrix(rows, 4, &count));
  TEST_ASSERT_EQUAL_UINT8(2, count);

  /* Explicit remove */
  const sys_power_rail_ref_t ref = {.vreg = DEV_RAIL1};
  TEST_ASSERT_NULL(sys_power_manager_remove_rail(&ref));
  TEST_ASSERT_NULL(sys_power_manager_get_matrix(rows, 4, &count));
  TEST_ASSERT_EQUAL_UINT8(1, count);

  /* Auto-remove on SYS_DEVICE_EV_REMOVED */
  TEST_ASSERT_NULL(sys_power_manager_add_rail(&r));
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_SYS_DEVICE, OP_REMOVE));
  vTaskDelay(pdMS_TO_TICKS(30));
  TEST_ASSERT_NULL(sys_power_manager_get_matrix(rows, 4, &count));
  TEST_ASSERT_EQUAL_UINT8(1, count);
}

static void test_three_budget_modes_and_interceptor_clamping(void) {
  create_mock(DEV_RAIL1);
  const sys_power_layout_t lay = base_layout();  // 10000 mW budget, 2000 mW reserve -> 8000 mW avail
  TEST_ASSERT_NULL(sys_power_manager_set_layout(&lay));
  const sys_power_rail_t r = {.vreg = DEV_RAIL1, .monitor_device = 0xFFFFu, .min_mA = 200};
  TEST_ASSERT_NULL(sys_power_manager_add_rail(&r));

  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_VOLTAGE, .voltage_mV = 5000));
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_ENABLE, .enable = 1));

  /* STRICT: asking 3000 mA (15000 mW > 8000 mW) clamps packet to 1600 mA and emits LIMIT_REACHED */
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_CURRENT, .current_mA = 3000));
  TEST_ASSERT_EQUAL_UINT32(1600, s_mock[DEV_RAIL1]->vreg_mA);
  vTaskDelay(pdMS_TO_TICKS(20));
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_ev_limit));

  /* MARGIN (+25% -> 12500 mW limit -> 10500 mW avail -> 2100 mA at 5 V) */
  const sys_power_manager_cfg_t cfg_m = {.mode = SYS_POWER_BUDGET_MARGIN, .soft_margin_pct = 25, .source = SYS_POWER_SOURCE_UNKNOWN};
  TEST_ASSERT_NULL(sys_power_manager_set_config(&cfg_m));
  vTaskDelay(pdMS_TO_TICKS(30));
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_CURRENT, .current_mA = 3000));
  TEST_ASSERT_EQUAL_UINT32(2100, s_mock[DEV_RAIL1]->vreg_mA);

  /* OP_VOLTAGE raised to 10 V while fixed at 2100 mA -> lowers current to 1050 mA to fit 10500 mW */
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_VOLTAGE, .voltage_mV = 10000));
  TEST_ASSERT_EQUAL_UINT32(10000, s_mock[DEV_RAIL1]->vreg_mV);
  TEST_ASSERT_EQUAL_UINT32(1050, s_mock[DEV_RAIL1]->vreg_mA);

  /* DISABLED: 4000 mA passes unclamped */
  const sys_power_manager_cfg_t cfg_d = {.mode = SYS_POWER_BUDGET_DISABLED, .soft_margin_pct = 25, .source = SYS_POWER_SOURCE_UNKNOWN};
  TEST_ASSERT_NULL(sys_power_manager_set_config(&cfg_d));
  vTaskDelay(pdMS_TO_TICKS(30));
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_CURRENT, .current_mA = 4000));
  TEST_ASSERT_EQUAL_UINT32(4000, s_mock[DEV_RAIL1]->vreg_mA);
}

static void test_voltage_clamps_when_below_min_mA_and_auto_rails_split_pool(void) {
  create_mock(DEV_RAIL1);
  create_mock(DEV_RAIL2);
  /* Budget 5000 mW, reserve 3000 mW -> 2000 mW avail. Rail1 min_mA = 500 -> max voltage at 500 mA is 4000 mV. */
  sys_power_layout_t lay = base_layout();
  lay.max_mV = 10000;
  lay.unknown_source_mA = 500;  // 10000 * 500 / 1000 = 5000 mW
  lay.reserve_mW = 3000;
  TEST_ASSERT_NULL(sys_power_manager_set_layout(&lay));

  const sys_power_rail_t r1 = {.vreg = DEV_RAIL1, .monitor_device = 0xFFFFu, .min_mA = 500};
  const sys_power_rail_t r2 = {.vreg = DEV_RAIL2, .monitor_device = 0xFFFFu, .min_mA = 100};
  TEST_ASSERT_NULL(sys_power_manager_add_rail(&r1));
  TEST_ASSERT_NULL(sys_power_manager_add_rail(&r2));

  /* Rail1 fixed at 500 mA, 2000 mV (1000 mW), enabled. Raising voltage to 8000 mV -> clamped to 4000 mV @ 500 mA! */
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_VOLTAGE, .voltage_mV = 2000));
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_CURRENT, .current_mA = 500));
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_ENABLE, .enable = 1));
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_VOLTAGE, .voltage_mV = 8000));
  TEST_ASSERT_EQUAL_UINT32(4000, s_mock[DEV_RAIL1]->vreg_mV);
  TEST_ASSERT_EQUAL_UINT32(500, s_mock[DEV_RAIL1]->vreg_mA);

  /* Now Rail1 uses all 2000 mW -> enabling automatic Rail2 must fail with ERR_PWRM_BUDGET_EXCEEDED! */
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL2, CONTRACT_VREG, OP_VOLTAGE, .voltage_mV = 5000));
  err_h e = SYS_DEVICE_SET(DEV_RAIL2, CONTRACT_VREG, OP_ENABLE, .enable = 1);
  TEST_ASSERT_EQUAL_HEX32(ERR_PWRM_BUDGET_EXCEEDED, err_root_tag(e));
  err_free(e);
}

static void test_source_detection_pd_and_lipo_curve(void) {
  create_mock(DEV_BUS);
  s_mock[DEV_BUS]->pin_level[0] = 1;  // usb_ok
  s_mock[DEV_BUS]->ch_mV[0] = 15000;

  sys_power_layout_t lay = {
      .max_mV = 20000,
      .max_mA = 5000,
      .reserve_mW = 1000,
      .unknown_source_mA = 500,
      .vreg_efficiency_pct = 100,
      .input_device = DEV_BUS,
      .input_channel = 0,
      .usb_pd = DEV_BUS,
      .usb_ok = {.device = DEV_BUS, .pin = 0},
      .ext_ok = {.device = DEV_BUS, .pin = 1},
  };
  TEST_ASSERT_NULL(sys_power_manager_set_layout(&lay));

  sys_power_status_t st;
  TEST_ASSERT_NULL(sys_power_manager_get_status(&st));
  TEST_ASSERT_EQUAL_UINT8(SYS_POWER_SOURCE_USB, st.source);
  TEST_ASSERT_EQUAL_UINT32(15000, st.pd_mV);
  TEST_ASSERT_EQUAL_UINT32(3000, st.pd_mA);
  TEST_ASSERT_EQUAL_UINT32(45000, st.budget_mW);

  /* Requesting 12 V with only fixed 5/9/15 V profiles selects 9 V and emits PD_FIXED */
  const sys_power_pd_t pd12 = {.mode = SYS_POWER_PD_VOLTAGE, .voltage_mV = 12000};
  TEST_ASSERT_NULL(sys_power_manager_set_pd(&pd12));
  vTaskDelay(pdMS_TO_TICKS(40));
  TEST_ASSERT_EQUAL_UINT32(9000, s_mock[DEV_BUS]->pd.voltage_mV);
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_ev_pd_fixed));

  /* Switch to 3S LiPo battery on ext_ok: 3 * 3840 mV = 50%, then drop to 3 * 3690 mV = 10% (low & critical) */
  const sys_power_battery_t bat = {.chemistry = SYS_POWER_BATTERY_LIPO, .cells = 3, .low_pct = 25, .critical_pct = 12};
  TEST_ASSERT_NULL(sys_power_manager_set_battery(&bat));
  s_mock[DEV_BUS]->pin_level[1] = 1;  // ext_ok wins
  s_mock[DEV_BUS]->ch_mV[0] = 3 * 3840;
  vTaskDelay(pdMS_TO_TICKS(80));
  TEST_ASSERT_NULL(sys_power_manager_get_status(&st));
  TEST_ASSERT_EQUAL_UINT8(SYS_POWER_SOURCE_BATTERY, st.source);
  TEST_ASSERT_EQUAL_UINT8(50, st.battery_pct);

  s_mock[DEV_BUS]->ch_mV[0] = 3 * 3690;  // 10%
  vTaskDelay(pdMS_TO_TICKS(80));
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_ev_bat_low));
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_ev_bat_crit));
}

static void test_unified_fault_event_codes_and_response_matrix(void) {
  create_mock(DEV_RAIL1);
  TEST_ASSERT_NULL(sys_power_manager_init(on_safe_state));
  const sys_power_rail_t r = {.vreg = DEV_RAIL1, .monitor_device = DEV_RAIL1, .monitor_channel = 0, .min_mA = 200};
  TEST_ASSERT_NULL(sys_power_manager_add_rail(&r));

  /* Set rail matrix: OVP -> RESET, OCP_CRITICAL -> DISABLE, SCP -> SAFE_STATE */
  sys_power_response_row_t row = {.target = DEV_RAIL1};
  row.responses[SYS_POWER_KIND_OVP] = SYS_POWER_RESPONSE_RESET;
  row.responses[SYS_POWER_KIND_OCP_CRITICAL] = SYS_POWER_RESPONSE_DISABLE;
  row.responses[SYS_POWER_KIND_SPC] = SYS_POWER_RESPONSE_SAFE_STATE;
  TEST_ASSERT_NULL(sys_power_manager_set_matrix(&row, 1));

  /* VREG_EV_OVP -> RESET */
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(VREG_EV_OVP, DEV_RAIL1, 0, .mV = 21000));
  vTaskDelay(pdMS_TO_TICKS(30));
  TEST_ASSERT_EQUAL_INT(1, s_mock[DEV_RAIL1]->resets);
  TEST_ASSERT_FALSE(is_suspended(DEV_RAIL1));

  /* PMON_EV_OCP_CRITICAL -> DISABLE (suspend rail) */
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(PMON_EV_OCP_CRITICAL, DEV_RAIL1, 0, .mA = 4500));
  vTaskDelay(pdMS_TO_TICKS(30));
  TEST_ASSERT_TRUE(is_suspended(DEV_RAIL1));

  /* VREG_EV_SCP -> SAFE_STATE callback */
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(VREG_EV_SCP, DEV_RAIL1, 0));
  vTaskDelay(pdMS_TO_TICKS(30));
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_safe_state_calls));
}

static void test_efficiency_rebalance_return_to_auto_and_settle_rollback(void) {
  create_mock(DEV_RAIL1);
  create_mock(DEV_RAIL2);
  /* 10000 mW budget, 2000 mW reserve -> 8000 mW pool; 90% efficiency -> 7200 mW output total. */
  sys_power_layout_t lay = base_layout();
  lay.vreg_efficiency_pct = 90;
  TEST_ASSERT_NULL(sys_power_manager_set_layout(&lay));

  const sys_power_rail_t r1 = {.vreg = DEV_RAIL1, .monitor_device = 0xFFFFu, .min_mA = 100};
  const sys_power_rail_t r2 = {.vreg = DEV_RAIL2, .monitor_device = 0xFFFFu, .min_mA = 100};
  TEST_ASSERT_NULL(sys_power_manager_add_rail(&r1));
  TEST_ASSERT_NULL(sys_power_manager_add_rail(&r2));

  /* Enable Rail1 (auto) at 5 V -> gets full 8000 mW * 90% / 5 V = 1440 mA. */
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_VOLTAGE, .voltage_mV = 5000));
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_ENABLE, .enable = 1));
  TEST_ASSERT_EQUAL_UINT32(1440, s_mock[DEV_RAIL1]->vreg_mA);

  /* Enable Rail2 (auto) at 5 V -> both split pool equally: 4000 mW * 90% / 5 V = 720 mA each! */
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL2, CONTRACT_VREG, OP_VOLTAGE, .voltage_mV = 5000));
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL2, CONTRACT_VREG, OP_ENABLE, .enable = 1));
  TEST_ASSERT_EQUAL_UINT32(720, s_mock[DEV_RAIL1]->vreg_mA);
  TEST_ASSERT_EQUAL_UINT32(720, s_mock[DEV_RAIL2]->vreg_mA);

  /* Fix Rail1 at 1000 mA (input = 5000 * 100 / 90 = 5555 mW -> 2445 mW left for Rail2 -> 440 mA). */
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_CURRENT, .current_mA = 1000));
  TEST_ASSERT_EQUAL_UINT32(1000, s_mock[DEV_RAIL1]->vreg_mA);
  TEST_ASSERT_EQUAL_UINT32(440, s_mock[DEV_RAIL2]->vreg_mA);

  /* Return Rail1 to automatic via OP_CURRENT = 0 -> both back to 720 mA! */
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_CURRENT, .current_mA = 0));
  TEST_ASSERT_EQUAL_UINT32(720, s_mock[DEV_RAIL1]->vreg_mA);
  TEST_ASSERT_EQUAL_UINT32(720, s_mock[DEV_RAIL2]->vreg_mA);

  /* vreg_settle rollback: suspend Rail1, try fixing Rail1 at 1000 mA -> device rejects (ERR_DEV_SUSPENDED),
   * and vreg_settle gives the budget back so Rail2 returns to 720 mA! */
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_SYS_DEVICE, OP_SUSPEND));
  err_h e = SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_CURRENT, .current_mA = 1000);
  TEST_ASSERT_EQUAL_HEX32(ERR_DEV_SUSPENDED, err_root_tag(e));
  err_free(e);
  TEST_ASSERT_EQUAL_UINT32(720, s_mock[DEV_RAIL2]->vreg_mA);
}

static void test_input_ovp_budget_shrink_and_battery_hysteresis(void) {
  create_mock(DEV_BUS);
  create_mock(DEV_RAIL1);
  s_mock[DEV_BUS]->pin_level[1] = 1;  // ext_ok -> battery
  s_mock[DEV_BUS]->ch_mV[0] = 3 * 3840;  // 50%

  sys_power_layout_t lay = {
      .max_mV = 15000,
      .max_mA = 2000,
      .reserve_mW = 1000,
      .unknown_source_mA = 1000,
      .vreg_efficiency_pct = 100,
      .input_device = DEV_BUS,
      .input_channel = 0,
      .usb_pd = 0xFFFFu,
      .usb_ok = {.device = DEV_BUS, .pin = 0},
      .ext_ok = {.device = DEV_BUS, .pin = 1},
  };
  TEST_ASSERT_NULL(sys_power_manager_set_layout(&lay));
  const sys_power_battery_t bat = {.chemistry = SYS_POWER_BATTERY_LIPO, .cells = 3, .low_pct = 25, .critical_pct = 10};
  TEST_ASSERT_NULL(sys_power_manager_set_battery(&bat));

  /* Battery hysteresis (+3%): drop to 20% -> BATTERY_LOW once; rise to 26% (< 28%) -> stays latched;
   * drop to 20% again -> still 1; rise to 30% (>= 28%) -> re-armed; drop to 20% -> fires 2nd time! */
  s_mock[DEV_BUS]->ch_mV[0] = 3 * 3730;  // 20%
  vTaskDelay(pdMS_TO_TICKS(80));
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_ev_bat_low));
  s_mock[DEV_BUS]->ch_mV[0] = 3 * 3754;  // 26% (< 25+3)
  vTaskDelay(pdMS_TO_TICKS(80));
  s_mock[DEV_BUS]->ch_mV[0] = 3 * 3730;  // 20%
  vTaskDelay(pdMS_TO_TICKS(80));
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_ev_bat_low));
  s_mock[DEV_BUS]->ch_mV[0] = 3 * 3770;  // 30% (>= 28%) -> re-arms
  vTaskDelay(pdMS_TO_TICKS(80));
  s_mock[DEV_BUS]->ch_mV[0] = 3 * 3730;  // 20% -> fires again
  vTaskDelay(pdMS_TO_TICKS(80));
  TEST_ASSERT_EQUAL_INT(2, atomic_load(&s_ev_bat_low));

  /* INPUT_OVP when input_mV > max_mV (15000) */
  s_mock[DEV_BUS]->ch_mV[0] = 16000;
  vTaskDelay(pdMS_TO_TICKS(80));
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_ev_input_ovp));

  /* BUDGET_EXCEEDED when budget shrinks below already-committed fixed rail */
  s_mock[DEV_BUS]->ch_mV[0] = 12000;  // 24000 mW budget
  vTaskDelay(pdMS_TO_TICKS(80));
  const sys_power_rail_t r1 = {.vreg = DEV_RAIL1, .monitor_device = 0xFFFFu, .min_mA = 100};
  TEST_ASSERT_NULL(sys_power_manager_add_rail(&r1));
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_VOLTAGE, .voltage_mV = 10000));
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_CURRENT, .current_mA = 1500));  // 15000 mW + 1000 reserve = 16000 mW
  TEST_ASSERT_NULL(SYS_DEVICE_SET(DEV_RAIL1, CONTRACT_VREG, OP_ENABLE, .enable = 1));
  s_mock[DEV_BUS]->ch_mV[0] = 5000;  // budget drops to 10000 mW < 16000 mW committed -> BUDGET_EXCEEDED!
  vTaskDelay(pdMS_TO_TICKS(80));
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_ev_budget_exc));
}

int main(void) {
  TEST_ASSERT_NULL(sys_event_init());
  uint16_t sub = 0;
  TEST_ASSERT_NULL(sys_event_subscribe(
      (sys_event_key_t){.tag = EVT_TAG(MODULE_ID_PWRM, SYS_EVENT_ANY_CODE),
                        .source = SYS_EVENT_ANY_SOURCE,
                        .channel = SYS_EVENT_ANY_CHANNEL},
      on_pwrm_ev, NULL, SYS_EVENT_RUN_QUEUED_SYS, &sub));

  UNITY_BEGIN();
  RUN_TEST(test_stopped_until_layout_auto_start_and_stop);
  RUN_TEST(test_runtime_rail_add_remove_and_auto_remove);
  RUN_TEST(test_three_budget_modes_and_interceptor_clamping);
  RUN_TEST(test_voltage_clamps_when_below_min_mA_and_auto_rails_split_pool);
  RUN_TEST(test_source_detection_pd_and_lipo_curve);
  RUN_TEST(test_unified_fault_event_codes_and_response_matrix);
  RUN_TEST(test_efficiency_rebalance_return_to_auto_and_settle_rollback);
  RUN_TEST(test_input_ovp_budget_shrink_and_battery_hysteresis);
  return UNITY_END();
}
