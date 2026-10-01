#include <stdlib.h>
#include "device_ap33772s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power.h"

#define OWNER OWNER_DEVICE

#define AP33772S_ADDRESS 0x52
#define AP33772S_I2C_FREQUENCY 400000
#define AP33772S_PDO_COUNT 13  // slots 1-7 SPR, 8-13 EPR
#define AP33772S_MAX_SOFTWARE_VOLTAGE_MV 22000  // software limit, whatever the charger offers
#define AP33772S_MAX_CURRENT_MA 5000

#define AP33772S_TASK_STACK 3072
#define AP33772S_TASK_PRIORITY 5
#define AP33772S_KEEPALIVE_MS 500  // AVS request is repeated at this period

// Registers
#define CMD_SYSTEM 0x06
#define CMD_VOLTAGE 0x11
#define CMD_CURRENT 0x12
#define CMD_SRCPDO 0x20
#define CMD_PD_REQMSG 0x31

// Source PDO as the chip reports it (2 bytes per slot).
typedef struct {
  union {
    struct {
      unsigned int voltage_max : 8;
      unsigned int peak_current : 2;
      unsigned int current_max : 4;
      unsigned int type : 1;
      unsigned int detect : 1;
    } fixed;
    struct {
      unsigned int voltage_max : 8;
      unsigned int voltage_min : 2;
      unsigned int current_max : 4;
      unsigned int type : 1;
      unsigned int detect : 1;
    } pps;
    struct {
      unsigned int voltage_max : 8;
      unsigned int voltage_min : 2;
      unsigned int current_max : 4;
      unsigned int type : 1;
      unsigned int detect : 1;
    } avs;
    struct {
      uint8_t byte0;
      uint8_t byte1;
    };
  };
  uint32_t data;
} pdo_t;

// Request message (RDO) written to PD_REQMSG.
typedef struct {
  union {
    struct {
      unsigned int voltage_sel : 8;
      unsigned int current_sel : 4;
      unsigned int pdo_index : 4;
    } fields;
    struct {
      uint8_t byte0;
      uint8_t byte1;
    };
  };
  uint32_t data;
} rdo_t;

// Instance state: the whole device in one struct (chip data, pins, bus, task; the create cfg is not kept).
typedef struct ap_ctx_t {
  sys_device_base_t base;  // must be first
  sys_i2c_dev_t i2c;
  sys_io_pin_ref_t intr_pin;  // the INT pin of the create cfg, converted once at install

  TaskHandle_t keepalive_task;

  pdo_t pdo[AP33772S_PDO_COUNT];
  int index_pps;  // 1-based slot of the PPS / AVS profile the charger offers, -1 = none
  int index_avs;

  /* The last AVS request, repeated by the keep-alive task (the charger drops an AVS contract
     that is not refreshed). 16 bits, so the task never sees a half-updated request. */
  volatile bool avs_active;
  volatile uint16_t avs_request;

  // Tracked VREG target values
  uint32_t last_voltage_mV;
  uint32_t last_current_mA;
  bool is_enabled;
} ap_ctx_t;

// Install steps, recorded so teardown rolls back only what was actually built
enum { AP33772S_STEP_I2C_ADDED = 0, AP33772S_STEP_INTR_READY = 1, AP33772S_STEP_TASK = 2 };

/* ---- Chip access: err_h, the dispatcher above adds the device id ---- */

/* Code of a PDO current range: 0 = below 1.25 A, n = 1.00 + 0.25n A. */
static SE_MUST_USE err_h current_code(int current_mA, unsigned int* code) {
  SE_CHECK_IN_RANGE(current_mA, 0, AP33772S_MAX_CURRENT_MA);
  *code = (current_mA < 1250) ? 0 : ((current_mA - 1250) / 250) + 1;
  return NULL;
}

static SE_MUST_USE err_h chip_send_request(ap_ctx_t* c, rdo_t rdo) {
  const uint8_t payload[2] = {rdo.byte0, rdo.byte1};
  return sys_i2c_reg_write(&c->i2c, CMD_PD_REQMSG, payload, sizeof(payload));
}

/* Reads the source PDOs and finds the PPS / AVS profile. Waits for the chip to have them ready. */
static SE_MUST_USE err_h chip_read_pdos(ap_ctx_t* c) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    vTaskDelay(pdMS_TO_TICKS(100));
  } else {
    ets_delay_us(100000);
  }

  uint8_t raw[2 * AP33772S_PDO_COUNT] = {0};
  SE_TRY(sys_i2c_reg_read(&c->i2c, CMD_SRCPDO, raw, sizeof(raw)));
  for (int i = 0; i < AP33772S_PDO_COUNT; i++) {
    c->pdo[i].byte0 = raw[2 * i];
    c->pdo[i].byte1 = raw[2 * i + 1];
  }

  for (int slot = 1; slot <= AP33772S_PDO_COUNT; slot++) {
    if (slot < 8 && c->pdo[slot - 1].pps.type == 1) {
      c->index_pps = slot;
    } else if (slot >= 8 && c->pdo[slot - 1].avs.type == 1) {
      c->index_avs = slot;
    }
  }
  return NULL;
}

static SE_MUST_USE err_h chip_set_fixed(ap_ctx_t* c, int slot, int current_mA) {
  SE_CHECK_IN_RANGE(slot, 1, AP33772S_PDO_COUNT);
  SE_CHECK_IN_RANGE(current_mA, 1, AP33772S_MAX_CURRENT_MA);
  c->avs_active = false;  // a fixed profile ends any keep-alive

  const pdo_t pdo = c->pdo[slot - 1];
  if (pdo.fixed.type != 0) SE_FAIL(ERR_BASE_INVALID_STATE, 0);

  int pdo_mV = pdo.fixed.voltage_max * ((slot >= 8) ? 200 : 100);
  SE_CHECK_IN_RANGE(pdo_mV, 0, AP33772S_MAX_SOFTWARE_VOLTAGE_MV);
  unsigned int code;
  SE_TRY(current_code(current_mA, &code));
  SE_CHECK_IN_RANGE(code, 0, pdo.fixed.current_max);

  rdo_t rdo = {0};
  rdo.fields.pdo_index = slot;
  rdo.fields.current_sel = code;
  return chip_send_request(c, rdo);
}

static SE_MUST_USE err_h chip_set_pps(ap_ctx_t* c, int slot, int voltage_mV, int current_mA) {
  SE_CHECK_IN_RANGE(slot, 1, 7);
  c->avs_active = false;  // a PPS profile ends any keep-alive

  const pdo_t pdo = c->pdo[slot - 1];
  if (pdo.pps.type != 1) SE_FAIL(ERR_BASE_INVALID_STATE, 0);

  if (voltage_mV > AP33772S_MAX_SOFTWARE_VOLTAGE_MV) voltage_mV = AP33772S_MAX_SOFTWARE_VOLTAGE_MV;
  unsigned int code;
  SE_TRY(current_code(current_mA, &code));
  SE_CHECK_IN_RANGE(code, 0, pdo.pps.current_max);
  SE_CHECK_IN_RANGE(voltage_mV, (pdo.pps.voltage_min > 0) ? 3300 : 0, pdo.pps.voltage_max * 100);

  rdo_t rdo = {0};
  rdo.fields.pdo_index = slot;
  rdo.fields.voltage_sel = voltage_mV / 100;
  rdo.fields.current_sel = code;
  return chip_send_request(c, rdo);
}

static SE_MUST_USE err_h chip_set_avs(ap_ctx_t* c, int slot, int voltage_mV, int current_mA) {
  SE_CHECK_IN_RANGE(slot, 8, AP33772S_PDO_COUNT);

  const pdo_t pdo = c->pdo[slot - 1];
  if (pdo.avs.type != 1) SE_FAIL(ERR_BASE_INVALID_STATE, 0);

  if (voltage_mV > AP33772S_MAX_SOFTWARE_VOLTAGE_MV) voltage_mV = AP33772S_MAX_SOFTWARE_VOLTAGE_MV;
  unsigned int code;
  SE_TRY(current_code(current_mA, &code));
  SE_CHECK_IN_RANGE(code, 0, pdo.avs.current_max);
  SE_CHECK_IN_RANGE(voltage_mV, (pdo.avs.voltage_min > 0) ? 15000 : 0, pdo.avs.voltage_max * 200);

  rdo_t rdo = {0};
  rdo.fields.pdo_index = slot;
  rdo.fields.voltage_sel = voltage_mV / 200;
  rdo.fields.current_sel = code;
  SE_TRY(chip_send_request(c, rdo));

  // The keep-alive task repeats this request every AP33772S_KEEPALIVE_MS
  c->avs_active = false;
  c->avs_request = (uint16_t)(rdo.byte0 | (rdo.byte1 << 8));
  c->avs_active = true;
  return NULL;
}

static SE_MUST_USE err_h chip_set_output(ap_ctx_t* c, bool enable) {
  const uint8_t flag = enable ? 0x12 : 0x11;
  return sys_i2c_reg_write(&c->i2c, CMD_SYSTEM, &flag, 1);
}

static SE_MUST_USE err_h chip_read_voltage_mV(ap_ctx_t* c, int32_t* mV) {
  uint8_t b[2];
  SE_TRY(sys_i2c_reg_read(&c->i2c, CMD_VOLTAGE, b, sizeof(b)));
  *mV = ((b[1] << 8) | b[0]) * 80;  // 80 mV per LSB
  return NULL;
}

static SE_MUST_USE err_h chip_read_current_mA(ap_ctx_t* c, int32_t* mA) {
  uint8_t v;
  SE_TRY(sys_i2c_reg_read(&c->i2c, CMD_CURRENT, &v, 1));
  *mA = v * 24;  // 24 mA per LSB
  return NULL;
}

/* Repeats the AVS request while one is active. A failure has no caller to return to,
   so it is raised here as a device error with this device's id. */
static void keepalive_task(void* arg) {
  ap_ctx_t* c = (ap_ctx_t*)arg;
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(AP33772S_KEEPALIVE_MS));
    if (!c->avs_active) continue;
    const uint16_t request = c->avs_request;
    const uint8_t payload[2] = {(uint8_t)request, (uint8_t)(request >> 8)};
    err_h err = sys_i2c_reg_write(&c->i2c, CMD_PD_REQMSG, payload, sizeof(payload));
    if (err) SE_push_to_handler(SYS_DEV_WRAP(err, SYS_DEV_GET_ID(c)));
  }
}

/* ---- Voltage regulator contract ---- */

static SE_MUST_USE err_h vreg_set_enable(void* handle, bool state) {
  SYS_DEV_CTX_FROM(ap_ctx_t, c, handle);
  c->is_enabled = state;
  return chip_set_output(c, state);
}

/* Asks the charger for the requested voltage / current: PPS when it fits, else AVS, else the
   highest fixed profile at or below the voltage. */
static SE_MUST_USE err_h negotiate_pdo(ap_ctx_t* c, uint32_t voltage_mV, uint32_t current_mA) {
  SE_CHECK_IN_RANGE(voltage_mV, 0, AP33772S_MAX_SOFTWARE_VOLTAGE_MV);

  // 1. Try PPS (a refusal falls through to the next kind)
  if (c->index_pps != -1) {
    const pdo_t pdo = c->pdo[c->index_pps - 1];
    if (voltage_mV >= ((pdo.pps.voltage_min > 0) ? 3300u : 0u) && voltage_mV <= pdo.pps.voltage_max * 100u) {
      err_h err = chip_set_pps(c, c->index_pps, voltage_mV, current_mA);
      if (!err) return NULL;
      SE_release(err);
    }
  }

  // 2. Try AVS
  if (c->index_avs != -1) {
    const pdo_t pdo = c->pdo[c->index_avs - 1];
    if (voltage_mV >= ((pdo.avs.voltage_min > 0) ? 15000u : 0u) && voltage_mV <= pdo.avs.voltage_max * 200u) {
      err_h err = chip_set_avs(c, c->index_avs, voltage_mV, current_mA);
      if (!err) return NULL;
      SE_release(err);
    }
  }

  // 3. Fall back to a fixed profile
  int best_slot = -1;
  int best_diff = 1000000;
  uint32_t lowest_fixed_mV = AP33772S_MAX_SOFTWARE_VOLTAGE_MV;

  for (int slot = 1; slot <= AP33772S_PDO_COUNT; slot++) {
    const pdo_t pdo = c->pdo[slot - 1];
    if (pdo.fixed.type == 0 && (pdo.byte0 != 0 || pdo.byte1 != 0)) {
      int pdo_mV = pdo.fixed.voltage_max * ((slot >= 8) ? 200 : 100);
      if ((uint32_t)pdo_mV < lowest_fixed_mV) lowest_fixed_mV = (uint32_t)pdo_mV;
      if (pdo_mV <= (int)voltage_mV && (int)voltage_mV - pdo_mV < best_diff) {
        best_diff = (int)voltage_mV - pdo_mV;
        best_slot = slot;
      }
    }
  }

  // No offered fixed profile is at or below the request: report the usable range
  if (best_slot == -1) {
    SE_FAIL(ERR_INVALID_VAL_UI32, .val = voltage_mV, .min = lowest_fixed_mV, .max = AP33772S_MAX_SOFTWARE_VOLTAGE_MV);
  }
  return chip_set_fixed(c, best_slot, current_mA);
}

static SE_MUST_USE err_h vreg_set_voltage(void* handle, uint32_t voltage_mV) {
  SYS_DEV_CTX_FROM(ap_ctx_t, c, handle);
  c->last_voltage_mV = voltage_mV;
  return negotiate_pdo(c, c->last_voltage_mV, c->last_current_mA);
}

static SE_MUST_USE err_h vreg_set_current(void* handle, uint32_t current_mA) {
  SYS_DEV_CTX_FROM(ap_ctx_t, c, handle);
  c->last_current_mA = current_mA;
  return negotiate_pdo(c, c->last_voltage_mV, c->last_current_mA);
}

static const sys_power_vreg_contract_t s_ap33772s_vreg_contract = {
    .set_enable = vreg_set_enable, .set_voltage = vreg_set_voltage, .set_current = vreg_set_current};

/* ---- USB PD contract ---- */

static SE_MUST_USE err_h usb_pd_set_settings(void* handle, uint32_t voltage_mV, uint32_t current_mA) {
  SYS_DEV_CTX_FROM(ap_ctx_t, c, handle);
  c->last_voltage_mV = voltage_mV;
  c->last_current_mA = current_mA;
  return negotiate_pdo(c, voltage_mV, current_mA);
}

/* Upper end of a PDO current range code (datasheet: 0 = below 1.25 A,
   n = 1.00 + 0.25n A, 14 = 4.50 A, 15 = 5.00 A and above). Same code for
   fixed, PPS and AVS objects. */
static uint16_t pdo_max_mA(unsigned int code) {
  if (code >= 15) return 5000;
  if (code == 14) return 4500;
  return (uint16_t)(code * 250 + 1250);
}

/* Decode source PDO `slot` (1-based; slots 8-13 are EPR). False if empty. */
static bool pdo_to_option(const pdo_t* pdo, uint8_t slot, sys_power_usb_pd_option_t* out) {
  if (pdo->byte0 == 0 && pdo->byte1 == 0) return false;
  bool is_epr = (slot >= 8);
  out->slot = slot;
  out->max_mA = pdo_max_mA(pdo->fixed.current_max);
  if (pdo->fixed.type == 0) {
    out->type = SYS_POWER_USB_PD_OPTION_FIXED;
    out->max_mV = (uint16_t)(pdo->fixed.voltage_max * (is_epr ? 200 : 100));
    out->min_mV = out->max_mV;
  } else if (!is_epr) {
    out->type = SYS_POWER_USB_PD_OPTION_PPS;
    out->max_mV = (uint16_t)(pdo->pps.voltage_max * 100);
    out->min_mV = (pdo->pps.voltage_min > 0) ? 3300 : 0;
  } else {
    out->type = SYS_POWER_USB_PD_OPTION_AVS;
    out->max_mV = (uint16_t)(pdo->avs.voltage_max * 200);
    out->min_mV = (pdo->avs.voltage_min > 0) ? 15000 : 0;
  }
  return true;
}

static SE_MUST_USE err_h usb_pd_list_options(void* handle, sys_power_usb_pd_option_t* out_options, uint8_t max_options, uint8_t* out_count) {
  SYS_DEV_CTX_FROM(ap_ctx_t, c, handle);
  uint8_t count = 0;
  for (uint8_t slot = 1; slot <= AP33772S_PDO_COUNT && count < max_options; slot++) {
    if (pdo_to_option(&c->pdo[slot - 1], slot, &out_options[count])) count++;
  }
  *out_count = count;
  return NULL;
}

static SE_MUST_USE err_h usb_pd_get_limits(void* handle, int32_t* out_mV, int32_t* out_mA) {
  SYS_DEV_CTX_FROM(ap_ctx_t, c, handle);
  SE_CHECK_NOT_NULL(out_mV);
  SE_CHECK_NOT_NULL(out_mA);

  uint32_t max_mV = 0;
  uint32_t max_mA = 0;

  // The highest-voltage offer sets the limits.
  for (uint8_t slot = 1; slot <= AP33772S_PDO_COUNT; slot++) {
    sys_power_usb_pd_option_t option;
    if (pdo_to_option(&c->pdo[slot - 1], slot, &option) && option.max_mV > max_mV) {
      max_mV = option.max_mV;
      max_mA = option.max_mA;
    }
  }

  *out_mV = (int32_t)max_mV;
  *out_mA = (int32_t)max_mA;
  return NULL;
}

static const sys_power_usb_pd_contract_t s_ap33772s_usb_pd_contract = {
    .set_settings = usb_pd_set_settings, .list_options = usb_pd_list_options, .get_limits = usb_pd_get_limits};

/* ---- Power monitor contract (single rail: the channel is not used) ---- */

static SE_MUST_USE err_h monitor_get_voltage(void* handle, uint8_t channel, int32_t* out_mV) {
  SYS_DEV_CTX_FROM(ap_ctx_t, c, handle);
  (void)channel;
  SE_CHECK_NOT_NULL(out_mV);
  return chip_read_voltage_mV(c, out_mV);
}

static SE_MUST_USE err_h monitor_get_current(void* handle, uint8_t channel, int32_t* out_mA) {
  SYS_DEV_CTX_FROM(ap_ctx_t, c, handle);
  (void)channel;
  SE_CHECK_NOT_NULL(out_mA);
  return chip_read_current_mA(c, out_mA);
}

static const sys_power_monitor_contract_t s_ap33772s_monitor_contract = {.get_voltage = monitor_get_voltage, .get_current = monitor_get_current};

/* ---- Lifecycle ---- */

// Doubles as the install rollback path: each step is gated on having actually
// run, and no step may early-return - teardown must always free everything.
static SE_MUST_USE err_h device_uninstall(void* handle) {
  SYS_DEV_CTX_FROM(ap_ctx_t, c, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(c, AP33772S_STEP_TASK) {
    vTaskDelete(c->keepalive_task);
  }
  IF_SYS_DEV_STEP_DONE(c, AP33772S_STEP_INTR_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(c->intr_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->intr_pin));
  }
  IF_SYS_DEV_STEP_DONE(c, AP33772S_STEP_I2C_ADDED) {
    SYS_DEV_TEARDOWN_STEP(err, sys_i2c_dev_remove(&c->i2c));
  }

  free(c);
  return err;
}

// The output is the board's own supply, so reset must not switch it off either (brownout).
static SE_MUST_USE err_h device_reset(void* handle) {
  (void)handle;
  return NULL;
}

// The output is the board's own supply (the sink input): switching it off in a fault would
// cut power to the ESP (brownout, tested 2026-10-01), so suspend / resume leave it alone.
static SE_MUST_USE err_h device_suspend(void* handle) {
  (void)handle;
  return NULL;
}

static SE_MUST_USE err_h device_resume(void* handle) {
  (void)handle;
  return NULL;
}

static SE_MUST_USE err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_ap33772s_cfg_t* cfg = (const d_ap33772s_cfg_t*)cfg_blob;

  // The cfg is read only here: what the device keeps (the INT pin) goes into its own state.
  ap_ctx_t* c = (ap_ctx_t*)calloc(1, sizeof(ap_ctx_t));
  SE_CHECK_IF_ALLOCATED(c);
  c->base.device_id = cfg->device_id;
  c->intr_pin = cfg->intr_pin;
  c->index_pps = c->index_avs = -1;
  c->last_voltage_mV = 5000;
  c->last_current_mA = 500;
  err_h err = NULL;

  sys_i2c_dev_init(&c->i2c, cfg->i2c_bus != 0, cfg->i2c_addr, AP33772S_I2C_FREQUENCY);
  SYS_DEV_INSTALL_STEP_BIT(c, AP33772S_STEP_I2C_ADDED, sys_i2c_dev_add(&c->i2c), "i2c add (probes the chip)");

  if (sys_io_pin_is_valid(c->intr_pin)) {
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(c->intr_pin), "intr pin mode");
    sys_io_intr_config_t config = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE};
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(c->intr_pin, &config), "intr pin configure");
    SYS_DEV_INSTALL_STEP_BIT(c, AP33772S_STEP_INTR_READY, sys_io_lock_pin(c->intr_pin), "intr pin lock");
  }

  SYS_DEV_INSTALL_STEP(chip_read_pdos(c), "read source PDOs");

  // The task only repeats an AVS request, so it starts once the PDOs are known
  if (xTaskCreate(keepalive_task, "ap33772s_svc", AP33772S_TASK_STACK, c, AP33772S_TASK_PRIORITY, &c->keepalive_task) != pdPASS) {
    err = SE_ERR_NEW(ERR_BASE_NO_MEM, 0);
    goto fail;
  }
  SYS_DEV_STEP_DONE(c, AP33772S_STEP_TASK);

  *out_device_handle = c;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, out_device_handle, device_uninstall, c);
  return NULL;
}

static const uint8_t s_pin_refs[] = {offsetof(d_ap33772s_cfg_t, intr_pin)};

const sys_device_class_t g_ap33772s_class = {
    .type_id = AP33772S_TYPE_ID,
    .cfg_size = sizeof(d_ap33772s_cfg_t),
    SYS_DEVICE_PINS(s_pin_refs),
    .name = "AP33772S",
    .contracts = {[SYS_DEVICE_CONTRACT_POWER_VREG] = &s_ap33772s_vreg_contract,
        [SYS_DEVICE_CONTRACT_POWER_USB_PD] = &s_ap33772s_usb_pd_contract,
        [SYS_DEVICE_CONTRACT_POWER_MONITOR] = &s_ap33772s_monitor_contract},
    .ops = {.install = device_install,
        .uninstall = device_uninstall,
        .reset = device_reset,
        .suspend = device_suspend,
        .resume = device_resume},
};
