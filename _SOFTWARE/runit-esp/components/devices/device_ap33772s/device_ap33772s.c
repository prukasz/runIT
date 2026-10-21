#include "device_ap33772s.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include "sys_i2c.h"
#include "sys_pins.h"
#include "utils.h"

#define I2C_SPEED_HZ            400000u
#define PDO_COUNT               SYS_POWER_USB_PD_OPTIONS_MAX  // slots 1-7 SPR, 8-13 EPR
#define MAX_SOFTWARE_VOLTAGE_MV 22000u          // software limit, whatever the charger offers
#define MAX_CURRENT_MA          5000u
#define PDO_WAIT_MS             100u        // the chip has the source PDOs this long after power-up
#define KEEPALIVE_MS            500u       // an AVS request is repeated at this period
#define DEFAULT_VOLTAGE_MV      5000u
#define DEFAULT_CURRENT_MA      500u
#define VSAFE_5V_MV             5000u  // the 5 V every USB-PD source offers (vSafe5V)

#define CMD_SYSTEM    0x06u
#define CMD_VOLTAGE   0x11u
#define CMD_CURRENT   0x12u
#define CMD_SRCPDO    0x20u
#define CMD_PD_REQMSG 0x31u

#define OUTPUT_ON  0x12u
#define OUTPUT_OFF 0x11u

/* Own op: repeat the AVS request, sent by the keep-alive timer through sys_device_call (so it runs under the
 * registry lock like any op). Harmless from outside: without an AVS contract it does nothing. */
#define AP_OP_KEEPALIVE SYS_DEVICE_OWN_OP(0x00)

/* A source PDO as the chip reports it (2 bytes per slot, little endian):
 *   bits 0-7 voltage max, 8-9 peak current (fixed) / voltage-min flag (PPS, AVS), 10-13 current max,
 *   14 type (0 fixed, 1 PPS in slots 1-7 / AVS in slots 8-13), 15 detect. */
static inline uint8_t pdo_vmax(uint16_t p) { return (uint8_t)(p & 0xFFu); }
static inline bool pdo_has_vmin(uint16_t p) { return ((p >> 8) & 0x3u) > 0; }
static inline uint8_t pdo_imax_code(uint16_t p) { return (uint8_t)((p >> 10) & 0xFu); }
static inline bool pdo_augmented(uint16_t p) { return ((p >> 14) & 1u) != 0; }

typedef struct {
  sys_device_base_t base;  // first: id, suspended
  sys_i2c_dev_t i2c;
  sys_pins_owned_t intr;
  esp_timer_handle_t keepalive_timer;  // periodic while an AVS contract is active

  uint16_t pdo[PDO_COUNT];  // slot - 1
  int index_pps;            // 1-based slot of the PPS / AVS profile the charger offers, -1 = none
  int index_avs;

  bool avs_active;
  uint16_t avs_request;  // the last AVS request, repeated by the keep-alive

  uint32_t last_voltage_mV;  // the last request the charger took: a refused one leaves them; each write renegotiates with both
  uint32_t last_current_mA;
} ap_t;

/* ---- Chip access ---- */

// Code of a PDO current range: 0 = below 1.25 A, n = 1.00 + 0.25n A up to the next code, 15 = the top range (4 bits).
static unsigned current_code(uint32_t mA) {
  const unsigned code = mA < 1250 ? 0u : ((mA - 1250) / 250) + 1;
  return code > 15u ? 15u : code;
}

// The most a request may ask of a PDO whose current range code is `code`: the top of that range (code 9, 3.25-3.49 A: 3499 mA),
// so that current_code() of it is `code` again; 15, the top range, up to MAX_CURRENT_MA.
static uint16_t pdo_max_mA(unsigned code) {
  return code >= 15 ? (uint16_t)MAX_CURRENT_MA : (uint16_t)(code * 250 + 1249);
}

static err_h send_request(ap_t* c, unsigned voltage_sel, unsigned current_sel, unsigned slot) {
  const uint16_t rdo = (uint16_t)((voltage_sel & 0xFFu) | ((current_sel & 0xFu) << 8) | ((slot & 0xFu) << 12));
  const uint8_t payload[2] = {(uint8_t)rdo, (uint8_t)(rdo >> 8)};
  return sys_i2c_reg_write(&c->i2c, CMD_PD_REQMSG, payload, sizeof(payload));
}

static void keepalive_stop(ap_t* c) {
  c->avs_active = false;
  if (c->keepalive_timer != NULL) {
    (void)esp_timer_stop(c->keepalive_timer);  // ESP_ERR_INVALID_STATE when it isn't running
  }
}

// The chip needs a moment after power-up to have the source PDOs.
static void wait_pdos_ready(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    vTaskDelay(pdMS_TO_TICKS(PDO_WAIT_MS));
  } else {
    ets_delay_us(PDO_WAIT_MS * 1000u);
  }
}

// Reads the source PDOs and finds the PPS / AVS profile. Called before each request and each read of the offers, so a
// charger attached after power-up is seen.
static err_h read_pdos(ap_t* c) {
  uint8_t raw[2 * PDO_COUNT] = {0};
  ERR_TRY(sys_i2c_reg_read(&c->i2c, CMD_SRCPDO, raw, sizeof(raw)));
  for (int i = 0; i < PDO_COUNT; i++) {
    c->pdo[i] = (uint16_t)(raw[2 * i] | (raw[2 * i + 1] << 8));
  }
  c->index_pps = c->index_avs = -1;
  for (int slot = 1; slot <= PDO_COUNT; slot++) {
    if (slot < 8 && pdo_augmented(c->pdo[slot - 1])) {
      c->index_pps = slot;
    } else if (slot >= 8 && pdo_augmented(c->pdo[slot - 1])) {
      c->index_avs = slot;
    }
  }
  return NULL;
}

static err_h set_fixed(ap_t* c, int slot, uint32_t current_mA) {
  const uint16_t pdo = c->pdo[slot - 1];
  SYS_DEV_CHECK_IN_RANGE(c->base.id, current_mA, 1, MAX_CURRENT_MA);
  const unsigned code = current_code(current_mA);
  ERR_CHECK(code <= pdo_imax_code(pdo), ERR_DEV_RANGE, .id = c->base.id, .value = (int32_t)current_mA, .min = 1,
            .max = pdo_max_mA(pdo_imax_code(pdo)));
  keepalive_stop(c);  // a fixed profile ends any keep-alive
  return send_request(c, 0, code, (unsigned)slot);
}

static err_h set_pps(ap_t* c, int slot, uint32_t voltage_mV, uint32_t current_mA) {
  const uint16_t pdo = c->pdo[slot - 1];
  voltage_mV = voltage_mV > MAX_SOFTWARE_VOLTAGE_MV ? MAX_SOFTWARE_VOLTAGE_MV : voltage_mV;
  ERR_CHECK(current_mA <= MAX_CURRENT_MA && current_code(current_mA) <= pdo_imax_code(pdo), ERR_DEV_RANGE, .id = c->base.id,
            .value = (int32_t)current_mA, .min = 0, .max = pdo_max_mA(pdo_imax_code(pdo)));
  const uint32_t vmin = pdo_has_vmin(pdo) ? 3300u : 0u;
  SYS_DEV_CHECK_IN_RANGE(c->base.id, voltage_mV, vmin, pdo_vmax(pdo) * 100);
  keepalive_stop(c);  // a PPS profile ends any keep-alive
  return send_request(c, voltage_mV / 100u, current_code(current_mA), (unsigned)slot);
}

static err_h set_avs(ap_t* c, int slot, uint32_t voltage_mV, uint32_t current_mA) {
  const uint16_t pdo = c->pdo[slot - 1];
  voltage_mV = voltage_mV > MAX_SOFTWARE_VOLTAGE_MV ? MAX_SOFTWARE_VOLTAGE_MV : voltage_mV;
  ERR_CHECK(current_mA <= MAX_CURRENT_MA && current_code(current_mA) <= pdo_imax_code(pdo), ERR_DEV_RANGE, .id = c->base.id,
            .value = (int32_t)current_mA, .min = 0, .max = pdo_max_mA(pdo_imax_code(pdo)));
  const uint32_t vmin = pdo_has_vmin(pdo) ? 15000u : 0u;
  SYS_DEV_CHECK_IN_RANGE(c->base.id, voltage_mV, vmin, pdo_vmax(pdo) * 200);
  const unsigned voltage_sel = voltage_mV / 200u;
  const unsigned code = current_code(current_mA);
  ERR_TRY(send_request(c, voltage_sel, code, (unsigned)slot));
  // The charger drops an AVS contract that isn't refreshed: the timer repeats this request.
  c->avs_request = (uint16_t)((voltage_sel & 0xFFu) | ((code & 0xFu) << 8) | (((unsigned)slot & 0xFu) << 12));
  c->avs_active = true;
  esp_err_t rc = esp_timer_start_periodic(c->keepalive_timer, (uint64_t)KEEPALIVE_MS * 1000u);
  return ERR_ESP(rc == ESP_ERR_INVALID_STATE ? ESP_OK : rc);  // already running: it repeats the new request
}

/* Asks the charger for the wanted voltage / current: PPS when it fits, else AVS, else the highest fixed profile at
 * or below the voltage. A refusal of one kind falls through to the next. */
static err_h negotiate(ap_t* c, uint32_t voltage_mV, uint32_t current_mA) {
  SYS_DEV_CHECK_IN_RANGE(c->base.id, voltage_mV, 0, MAX_SOFTWARE_VOLTAGE_MV);
  ERR_TRY(read_pdos(c));
  if (c->index_pps != -1) {
    const uint16_t pdo = c->pdo[c->index_pps - 1];
    if (voltage_mV >= (pdo_has_vmin(pdo) ? 3300u : 0u) && voltage_mV <= pdo_vmax(pdo) * 100u) {
      err_h err = set_pps(c, c->index_pps, voltage_mV, current_mA);
      if (err == NULL) {
        return NULL;
      }
      err_free(err);
    }
  }
  if (c->index_avs != -1) {
    const uint16_t pdo = c->pdo[c->index_avs - 1];
    if (voltage_mV >= (pdo_has_vmin(pdo) ? 15000u : 0u) && voltage_mV <= pdo_vmax(pdo) * 200u) {
      err_h err = set_avs(c, c->index_avs, voltage_mV, current_mA);
      if (err == NULL) {
        return NULL;
      }
      err_free(err);
    }
  }
  // The highest fixed profile at or below the request.
  int best_slot = -1;
  uint32_t best_diff = UINT32_MAX;
  uint32_t lowest_fixed_mV = 0;  // no fixed offer read: reported as vSafe5V below
  for (int slot = 1; slot <= PDO_COUNT; slot++) {
    const uint16_t pdo = c->pdo[slot - 1];
    if (pdo == 0 || pdo_augmented(pdo)) {
      continue;
    }
    const uint32_t pdo_mV = pdo_vmax(pdo) * (slot >= 8 ? 200u : 100u);
    lowest_fixed_mV = lowest_fixed_mV == 0 || pdo_mV < lowest_fixed_mV ? pdo_mV : lowest_fixed_mV;
    if (pdo_mV <= voltage_mV && voltage_mV - pdo_mV < best_diff) {
      best_diff = voltage_mV - pdo_mV;
      best_slot = slot;
    }
  }
  ERR_CHECK(best_slot != -1, ERR_POWER_NO_PROFILE, .id = c->base.id, .voltage_mV = voltage_mV,
            .lowest_mV = lowest_fixed_mV != 0 ? lowest_fixed_mV : VSAFE_5V_MV);
  return set_fixed(c, best_slot, current_mA);
}

/* ---- Keep-alive ---- */

// esp_timer task. Only the ID crosses over: a REMOVE stops the timer first, and a request already waiting for the
// lock then finds no device (ERR_DEV_NOT_FOUND, reported).
static void keepalive_cb(void* arg) {
  err_report(sys_device_write((uint16_t)(uintptr_t)arg, AP_OP_KEEPALIVE, NULL, 0));
}

SYS_DEVICE_HANDLER(keepalive_h, sys_device_none_t) {
  (void)args, (void)out, (void)out_cap, (void)out_len;
  ap_t* c = ctx;
  if (!c->avs_active) {
    return NULL;
  }
  const uint8_t payload[2] = {(uint8_t)c->avs_request, (uint8_t)(c->avs_request >> 8)};
  return sys_i2c_reg_write(&c->i2c, CMD_PD_REQMSG, payload, sizeof(payload));
}

/* ---- POWER_VREG: the sink's output switch and a wanted voltage / current ---- */

OP_WRITE(VREG, ENABLE) {
  ap_t* c = ctx;
  const uint8_t flag = args->enable ? OUTPUT_ON : OUTPUT_OFF;
  return sys_i2c_reg_write(&c->i2c, CMD_SYSTEM, &flag, 1);
}

OP_WRITE(VREG, VOLTAGE) {
  ap_t* c = ctx;
  ERR_TRY(negotiate(c, args->voltage_mV, c->last_current_mA));
  c->last_voltage_mV = args->voltage_mV;
  return NULL;
}

OP_WRITE(VREG, CURRENT) {
  ap_t* c = ctx;
  ERR_TRY(negotiate(c, c->last_voltage_mV, args->current_mA));
  c->last_current_mA = args->current_mA;
  return NULL;
}

/* ---- POWER_USB_PD ---- */

OP_WRITE(PD, SETTINGS) {
  ap_t* c = ctx;
  ERR_TRY(negotiate(c, args->voltage_mV, args->current_mA));
  c->last_voltage_mV = args->voltage_mV;
  c->last_current_mA = args->current_mA;
  return NULL;
}

// The last request the charger took (a refused one is not kept). A read: allowed while suspended.
OP_READ(PD, SETTINGS) {
  (void)args;
  const ap_t* c = ctx;
  const pd_settings_args_t a = {.voltage_mV = c->last_voltage_mV, .current_mA = c->last_current_mA};
  return cmd_reply(out, out_cap, out_len, &a, sizeof(a));
}

// Source PDO `slot` (1-based) as an option; false if the slot is empty.
static bool pdo_option(const ap_t* c, uint8_t slot, sys_power_usb_pd_option_t* out) {
  const uint16_t pdo = c->pdo[slot - 1];
  if (pdo == 0) {
    return false;
  }
  const bool epr = slot >= 8;
  out->slot = slot;
  out->max_mA = pdo_max_mA(pdo_imax_code(pdo));
  if (!pdo_augmented(pdo)) {
    out->type = SYS_POWER_USB_PD_OPTION_FIXED;
    out->max_mV = (uint16_t)(pdo_vmax(pdo) * (epr ? 200 : 100));
    out->min_mV = out->max_mV;
  } else if (!epr) {
    out->type = SYS_POWER_USB_PD_OPTION_PPS;
    out->max_mV = (uint16_t)(pdo_vmax(pdo) * 100);
    out->min_mV = pdo_has_vmin(pdo) ? 3300 : 0;
  } else {
    out->type = SYS_POWER_USB_PD_OPTION_AVS;
    out->max_mV = (uint16_t)(pdo_vmax(pdo) * 200);
    out->min_mV = pdo_has_vmin(pdo) ? 15000 : 0;
  }
  return true;
}

OP_READ(PD, OPTIONS) {
  (void)args;
  ap_t* c = ctx;
  ERR_TRY(read_pdos(c));
  sys_power_usb_pd_option_t options[PDO_COUNT];
  uint32_t count = 0;
  for (uint8_t slot = 1; slot <= PDO_COUNT; slot++) {
    if (pdo_option(c, slot, &options[count])) {
      count++;
    }
  }
  return cmd_reply(out, out_cap, out_len, options, count * (uint32_t)sizeof(options[0]));
}

// The highest-voltage offer sets the limits.
OP_READ(PD, LIMITS) {
  (void)args;
  ap_t* c = ctx;
  ERR_TRY(read_pdos(c));
  sys_power_usb_pd_limits_t limits = {0};
  uint32_t max_mV = 0;
  for (uint8_t slot = 1; slot <= PDO_COUNT; slot++) {
    sys_power_usb_pd_option_t option;
    if (pdo_option(c, slot, &option) && option.max_mV > max_mV) {
      max_mV = option.max_mV;
      limits = (sys_power_usb_pd_limits_t){.voltage_mV = option.max_mV, .current_mA = option.max_mA};
    }
  }
  return cmd_reply(out, out_cap, out_len, &limits, sizeof(limits));
}

/* ---- POWER_MONITOR: the sink's output, one channel (0). Readings are allowed while suspended. ---- */

static err_h check_channel(const ap_t* c, uint8_t channel) {
  ERR_CHECK(channel == 0, ERR_POWER_BAD_CHANNEL, .id = c->base.id, .channel = channel);
  return NULL;
}

static err_h read_voltage_mV(ap_t* c, int32_t* mV) {
  uint8_t b[2];
  ERR_TRY(sys_i2c_reg_read(&c->i2c, CMD_VOLTAGE, b, sizeof(b)));
  *mV = ((b[1] << 8) | b[0]) * 80;  // 80 mV per LSB
  return NULL;
}

static err_h read_current_mA(ap_t* c, int32_t* mA) {
  uint8_t v;
  ERR_TRY(sys_i2c_reg_read(&c->i2c, CMD_CURRENT, &v, 1));
  *mA = v * 24;  // 24 mA per LSB
  return NULL;
}

OP_READ(PMON, VOLTAGE) {
  ap_t* c = ctx;
  ERR_TRY(check_channel(c, args->channel));
  int32_t mV;
  ERR_TRY(read_voltage_mV(c, &mV));
  return cmd_reply(out, out_cap, out_len, &mV, sizeof(mV));
}

OP_READ(PMON, CURRENT) {
  ap_t* c = ctx;
  ERR_TRY(check_channel(c, args->channel));
  int32_t mA;
  ERR_TRY(read_current_mA(c, &mA));
  return cmd_reply(out, out_cap, out_len, &mA, sizeof(mA));
}

OP_READ(PMON, POWER) {
  ap_t* c = ctx;
  ERR_TRY(check_channel(c, args->channel));
  int32_t mV, mA;
  ERR_TRY(read_voltage_mV(c, &mV));
  ERR_TRY(read_current_mA(c, &mA));
  const int32_t mW = sys_power_mW(mV, mA);
  return cmd_reply(out, out_cap, out_len, &mW, sizeof(mW));
}

/* ---- Lifecycle: the output is the board's own supply (the sink input), so reset / suspend / resume leave it alone:
 * switching it off in a fault would cut power to the ESP (brownout, tested 2026-10-01). ---- */

OP_WRITE(SYS_DEVICE, RESET) {
  (void)ctx, (void)args, (void)out, (void)out_cap, (void)out_len;
  return NULL;
}

static err_h suspend_hook(void* ctx) {
  (void)ctx;
  return NULL;
}

static err_h resume_hook(void* ctx) {
  (void)ctx;
  return NULL;
}

// Releases what was built; each step runs only if it was built, none returns early.
static err_h uninstall(void* ctx) {
  ap_t* c = ctx;
  err_h err = NULL;
  if (c->keepalive_timer != NULL) {
    keepalive_stop(c);
    ERR_KEEP_FIRST(err, ERR_ESP(esp_timer_delete(c->keepalive_timer)));
    c->keepalive_timer = NULL;
  }
  ERR_KEEP_FIRST(err, sys_pins_release(&c->intr));
  if (sys_i2c_dev_added(&c->i2c)) {
    ERR_KEEP_FIRST(err, sys_i2c_dev_remove(&c->i2c));
  }
  return err;
}

static err_h create(void* ctx, const void* cfg_in) {
  ap_t* c = ctx;
  const ap33772s_cfg_t* cfg = cfg_in;
  const uint16_t id = c->base.id;
  SYS_DEV_CHECK_IN_RANGE(id, cfg->i2c_bus, 0, SYS_I2C_BUS_COUNT - 1);
  SYS_DEV_CHECK_IN_RANGE(id, cfg->i2c_addr, AP33772S_I2C_ADDR, AP33772S_I2C_ADDR);
  c->index_pps = c->index_avs = -1;
  c->last_voltage_mV = DEFAULT_VOLTAGE_MV;
  c->last_current_mA = DEFAULT_CURRENT_MA;
  const esp_timer_create_args_t timer_args = {
      .callback = keepalive_cb, .arg = (void*)(uintptr_t)id, .name = "ap_keepalive"};
  ERR_TRY(ERR_ESP(esp_timer_create(&timer_args, &c->keepalive_timer)));

  ERR_TRY(sys_i2c_dev_attach(&c->i2c, cfg->i2c_bus, cfg->i2c_addr, I2C_SPEED_HZ));  // probes the chip

  ERR_TRY(sys_pins_take_input(&c->base, &c->intr, cfg->intr_pin, 0, SYS_IO_INTR_MODE_FALLING_EDGE));  // no events
  wait_pdos_ready();
  return read_pdos(c);
}

/* ---- Type ---- */

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W_ALWAYS(SYS_DEVICE, RESET),
    OP_ENTRY_W(VREG, ENABLE),   OP_ENTRY_W(VREG, VOLTAGE),  OP_ENTRY_W(VREG, CURRENT),
    OP_ENTRY(PD, SETTINGS),     OP_ENTRY_R(PD, OPTIONS),    OP_ENTRY_R(PD, LIMITS),
    OP_ENTRY_R(PMON, VOLTAGE),  OP_ENTRY_R(PMON, CURRENT),  OP_ENTRY_R(PMON, POWER),
    /* Internal keepalive timer op */
    {.op = AP_OP_KEEPALIVE, .write = keepalive_h, .always = true},
};

SYS_DEVICE_TYPE_DEFINE(AP33772S, ap33772s_cfg_t, ap_t, k_ops)
