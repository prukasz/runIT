/*
 * ADS7128: eight-channel 12-bit ADC over I2C with a window comparator per channel and an ALERT pin. The whole device is
 * this file: nothing includes it, the rest of the firmware reaches it by its ID and the IO contract's ops (a channel is
 * a pin): VOLTAGE reads it, INTR arms its window (SYS_IO_INTR_ADC_WINDOW_OUTSIDE / _INSIDE), whose crossings are
 * IO_EV_ADC_OUTSIDE / IO_EV_ADC_INSIDE. Readings and thresholds scale the 12-bit code over `full_scale_mV`.
 *
 * With no window armed the chip converts on request (manual mode). With one armed its sequencer runs on its own and a
 * reading is the channel's latest result. A comparator's flag is latched and set again on every sample past the
 * threshold, so after a crossing the channel is armed with an exit watch (its threshold moved back by the hysteresis,
 * the other way round); its trip is the recovery and arms the entry watch again. One event per excursion.
 */
#include <string.h>

#include "sys_device_driver.h"
#include "sys_device_event.h"
#include "sys_event.h"
#include "sys_i2c.h"
#include "sys_io_driver.h"
#include "utils.h"

SYS_DEVICE_TYPE_DECLARE(ADS7128, 0x0047)

/*******************************************************************************
 * OWN_ADS7128 : own operations
 *   OP_ALERT - ALERT fell (LOCAL | ALWAYS | TAIL)
 ******************************************************************************/
#define OWN_ADS7128_OPERATIONS(X, P) \
  X(P, OP_ALERT, 0x00, cmd_none_t, sys_event_t, SYS_DEVICE_OP_LOCAL | SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_TAIL)

SYS_DEVICE_OWN_DECLARE(OWN_ADS7128)

#define ADS7128_CHANNELS 8u

/** @brief CREATE's struct (wire format). ALERT not wired: `.intr_pin = SYS_IO_PIN_NONE`. */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;         /**< The bus's device ID (an I2C contract device) */
  uint8_t i2c_addr;        /**< 7-bit I2C address (0x10 - 0x17) */
  sys_io_pin_t intr_pin;   /**< Active-low ALERT open-drain interrupt pin */
  uint16_t full_scale_mV;  /**< Full scale input voltage in mV (e.g. 20000 mV) */
} ads7128_cfg_t;

#define I2C_SPEED_HZ      100000u
#define MAX_CODE          0x0FFFu
#define RESET_DELAY_MS    5u   // the chip answers again this long after a software reset
#define MANUAL_READ_TRIES 3u   // a frame right after a channel change may still carry the previous channel
#define CLK_DIV           0x0Au  // ~1 kSPS on the low-power oscillator
#define HYSTERESIS_STEP   8u     // the 4-bit hysteresis field is applied as [hysteresis, 000b]
#define HYSTERESIS_MAX    0x0Fu
#define EVENT_COUNT_MAX   16u    // the 4-bit field counts n + 1 violations
#define ALERT_ROUNDS      16u    // ALERT handling reads the flags again until none is pending, at most this often

#define OP_SINGLE_WRITE 0x08u  // frame opcodes: [opcode][register][data...]
#define OP_BLOCK_WRITE  0x28u
#define OP_BLOCK_READ   0x30u

#define REG_GENERAL_CFG     0x01u
#define REG_DATA_CFG        0x02u
#define REG_OPMODE_CFG      0x04u
#define REG_SEQUENCE_CFG    0x10u
#define REG_MANUAL_CH_SEL   0x11u
#define REG_AUTO_SEQ_CH_SEL 0x12u
#define REG_ALERT_CH_SEL    0x14u
#define REG_EVENT_FLAG      0x18u
#define REG_EVENT_HIGH_FLAG 0x1Au
#define REG_EVENT_LOW_FLAG  0x1Cu
#define REG_EVENT_RGN       0x1Eu
#define REG_HYSTERESIS_CH0  0x20u  // four registers per channel: HYSTERESIS, HIGH_TH, EVENT_COUNT, LOW_TH
#define REG_RECENT_CH0_LSB  0xA0u  // two registers per channel

#define GENERAL_RST      0x01u
#define GENERAL_DWC_EN   0x10u  // digital window comparator on
#define GENERAL_STATS_EN 0x20u  // keeps RECENT_CHx updated
#define DATA_APPEND_CHID 0x10u  // channel ID appended to conversion data
#define OPMODE_AUTO      0x20u
#define OPMODE_LOW_POWER 0x10u
#define SEQ_START_AUTO   0x11u

/* A channel's comparator in the chip's terms: 12-bit codes, the 4-bit fields. In-band (EVENT_RGN 1) the chip flags
 * HIGH_TH < code < LOW_TH, so an inside window swaps its bounds (seen on the board; the datasheet misprints it). */
typedef struct {
  uint16_t high_th;  // MAX_CODE: no high side
  uint16_t low_th;   // 0: no low side
  uint8_t hysteresis;
  uint8_t event_count;
  bool in_band;
} window_t;

static const window_t k_off = {.high_th = MAX_CODE};

typedef struct {
  uint8_t id;
  sys_io_pins_t pins;
  sys_i2c_dev_t i2c;
  sys_io_claim_t intr;      // ALERT: its fall runs OP_ALERT
  uint16_t full_scale_mV;
  uint8_t armed;            // bit per channel: a window armed (INTR)
  uint8_t past;             // bit per channel: past its threshold, the exit watch armed
  window_t entry[ADS7128_CHANNELS];  // the window INTR armed
  window_t live[ADS7128_CHANNELS];   // what the chip has: the entry, or the exit watch while past
} ads_t;

#define SYS_DEVICE_CTX ads_t

/* ---- Scaling ---- */

static int32_t code_to_mV(const ads_t* c, uint16_t code) {
  return (int32_t)(((uint64_t)code * c->full_scale_mV + MAX_CODE / 2) / MAX_CODE);
}

static uint16_t mV_to_code(const ads_t* c, uint32_t mV) {
  const uint64_t code = ((uint64_t)mV * MAX_CODE + c->full_scale_mV / 2) / c->full_scale_mV;
  return code > MAX_CODE ? MAX_CODE : (uint16_t)code;
}

/* ---- Chip access ---- */

static err_h chip_write(ads_t* c, uint8_t reg, uint8_t val) {
  const uint8_t frame[3] = {OP_SINGLE_WRITE, reg, val};
  return sys_i2c_write(&c->i2c, frame, sizeof(frame));
}

static err_h chip_read_block(ads_t* c, uint8_t reg, uint8_t* data, size_t len) {
  const uint8_t frame[2] = {OP_BLOCK_READ, reg};
  return sys_i2c_write_read(&c->i2c, frame, sizeof(frame), data, len);
}

// The four registers of a channel's window: a threshold's bits [11:4] in its own register, [3:0] in its neighbour's.
static err_h chip_write_window(ads_t* c, uint8_t ch) {
  const window_t* w = &c->live[ch];
  const uint8_t frame[6] = {
      OP_BLOCK_WRITE,
      (uint8_t)(REG_HYSTERESIS_CH0 + ch * 4u),
      (uint8_t)(((w->high_th & 0x0F) << 4) | (w->hysteresis & 0x0F)),
      (uint8_t)(w->high_th >> 4),
      (uint8_t)(((w->low_th & 0x0F) << 4) | (w->event_count & 0x0F)),
      (uint8_t)(w->low_th >> 4),
  };
  return sys_i2c_write(&c->i2c, frame, sizeof(frame));
}

/* The comparator sees only codes the ADC converts: with a window armed the sequencer runs on its own clock, with none
 * the chip converts on request. The sequencer is stopped before any change (the datasheet asks it). */
static err_h chip_apply(ads_t* c) {
  uint8_t rgn = 0;
  for (uint8_t ch = 0; ch < ADS7128_CHANNELS; ch++) {
    rgn |= c->live[ch].in_band ? (uint8_t)(1u << ch) : 0u;
  }
  ERR_TRY(chip_write(c, REG_SEQUENCE_CFG, 0));
  ERR_TRY(chip_write(c, REG_EVENT_RGN, rgn));
  ERR_TRY(chip_write(c, REG_ALERT_CH_SEL, c->armed));
  if (c->armed == 0) {
    ERR_TRY(chip_write(c, REG_GENERAL_CFG, 0));
    return chip_write(c, REG_OPMODE_CFG, OPMODE_LOW_POWER | CLK_DIV);
  }
  ERR_TRY(chip_write(c, REG_AUTO_SEQ_CH_SEL, 0xFF));
  ERR_TRY(chip_write(c, REG_OPMODE_CFG, OPMODE_AUTO | OPMODE_LOW_POWER | CLK_DIV));
  ERR_TRY(chip_write(c, REG_GENERAL_CFG, GENERAL_DWC_EN | GENERAL_STATS_EN));
  return chip_write(c, REG_SEQUENCE_CFG, SEQ_START_AUTO);
}

// One channel's window to the chip; a flag left from the old thresholds would hold ALERT.
static err_h set_window(ads_t* c, uint8_t ch, const window_t* w) {
  c->live[ch] = *w;
  ERR_TRY(chip_write(c, REG_SEQUENCE_CFG, 0));
  ERR_TRY(chip_write_window(c, ch));
  ERR_TRY(chip_write(c, REG_EVENT_HIGH_FLAG, (uint8_t)(1u << ch)));
  ERR_TRY(chip_write(c, REG_EVENT_LOW_FLAG, (uint8_t)(1u << ch)));
  return chip_apply(c);
}

// Every register from the struct (create, reset, resume).
static err_h chip_restore(ads_t* c) {
  ERR_TRY(chip_write(c, REG_SEQUENCE_CFG, 0));
  ERR_TRY(chip_write(c, REG_DATA_CFG, DATA_APPEND_CHID));  // a manual read proves which channel it got
  for (uint8_t ch = 0; ch < ADS7128_CHANNELS; ch++) {
    ERR_TRY(chip_write_window(c, ch));
  }
  ERR_TRY(chip_write(c, REG_EVENT_HIGH_FLAG, 0xFF));
  ERR_TRY(chip_write(c, REG_EVENT_LOW_FLAG, 0xFF));
  return chip_apply(c);
}

static err_h chip_reset(ads_t* c) {
  c->armed = c->past = 0;
  for (uint8_t ch = 0; ch < ADS7128_CHANNELS; ch++) {
    c->entry[ch] = c->live[ch] = k_off;
  }
  ERR_TRY(chip_write(c, REG_GENERAL_CFG, GENERAL_RST));
  vTaskDelay(MSEC(RESET_DELAY_MS));
  return chip_restore(c);
}

/* Manual mode: the write moves the mux, the read that follows converts and returns [D11:D4][D3:D0, CHID]. Autonomous:
 * the channel's latest result from the statistics block (LSB register first, MSB aligned). */
static err_h chip_read_code(ads_t* c, uint8_t ch, uint16_t* code) {
  uint8_t rx[2];
  if (c->armed != 0) {
    ERR_TRY(chip_read_block(c, (uint8_t)(REG_RECENT_CH0_LSB + ch * 2u), rx, sizeof(rx)));
    *code = (uint16_t)(((rx[1] << 8) | rx[0]) >> 4);
    return NULL;
  }
  const uint8_t tx[3] = {OP_SINGLE_WRITE, REG_MANUAL_CH_SEL, ch};
  for (uint8_t i = 0; i < MANUAL_READ_TRIES; i++) {
    ERR_TRY(sys_i2c_write_read(&c->i2c, tx, sizeof(tx), rx, sizeof(rx)));
    if ((rx[1] & 0x0F) == ch) {
      *code = (uint16_t)(((rx[0] << 8) | rx[1]) >> 4);
      return NULL;
    }
  }
  return ERR_NEW(ERR_IO_BAD_READING, .id = c->id, .pin = ch);
}

/* ---- ALERT ---- */

/* The exit watch of a crossing, out of band: past a high threshold it waits for the code under high_th - hysteresis,
 * past a low one over low_th + hysteresis; inside a band it waits for either side. */
static window_t exit_watch(const window_t* e, bool high_fired, bool low_fired) {
  const uint16_t h = (uint16_t)(e->hysteresis * HYSTERESIS_STEP);
  const uint16_t below = e->high_th > h ? (uint16_t)(e->high_th - h) : 0;
  const uint16_t above = (uint32_t)e->low_th + h > MAX_CODE ? MAX_CODE : (uint16_t)(e->low_th + h);
  window_t w = {.high_th = MAX_CODE, .event_count = e->event_count};
  if (e->in_band || high_fired) {
    w.low_th = below;
  }
  if (e->in_band || low_fired) {
    w.high_th = above;
  }
  return w;
}

// One flagged channel: an entry watch's trip is published and swapped for the exit watch, an exit watch's for the entry.
static err_h on_channel(ads_t* c, uint8_t ch, bool high_fired, bool low_fired) {
  const uint8_t bit = (uint8_t)(1u << ch);
  if (c->past & bit) {
    c->past &= (uint8_t)~bit;
    return set_window(c, ch, &c->entry[ch]);
  }
  c->past |= bit;
  uint16_t code = 0;
  err_h err = chip_read_code(c, ch, &code);
  const int32_t mV = code_to_mV(c, code);
  ERR_KEEP_FIRST(err, c->entry[ch].in_band ? SYS_EVENT_PUBLISH(IO_EV_ADC_INSIDE, c->id, ch, .mV = mV)
                                           : SYS_EVENT_PUBLISH(IO_EV_ADC_OUTSIDE, c->id, ch, .mV = mV));
  const window_t w = exit_watch(&c->entry[ch], high_fired, low_fired);
  ERR_KEEP_FIRST(err, set_window(c, ch, &w));
  return err;
}

/*******************************************************************************
 * OWN_ADS7128 : OP_ALERT (event, LOCAL | ALWAYS | TAIL)
 * ALERT is the OR of the latched flags and the edge fires once: the flags are
 * read again until none is pending, so a flag set between the read and its
 * clear never holds ALERT low for good. Nothing while suspended.
 ******************************************************************************/
OP_DECLARE_EVENT(OWN_ADS7128, OP_ALERT) {
  if (sys_device_self_suspended()) {
    return NULL;
  }
  err_h err = NULL;
  for (uint8_t round = 0; round < ALERT_ROUNDS; round++) {
    uint8_t raw[REG_EVENT_LOW_FLAG - REG_EVENT_FLAG + 1];
    ERR_TRY(chip_read_block(c, REG_EVENT_FLAG, raw, sizeof(raw)));
    const uint8_t high = raw[REG_EVENT_HIGH_FLAG - REG_EVENT_FLAG];
    const uint8_t low = raw[REG_EVENT_LOW_FLAG - REG_EVENT_FLAG];
    const uint8_t pending = (uint8_t)((high | low) & c->armed);
    if (pending == 0) {
      break;
    }
    for (uint8_t ch = 0; ch < ADS7128_CHANNELS; ch++) {
      if (pending & (1u << ch)) {
        ERR_KEEP_FIRST(err, on_channel(c, ch, (high >> ch) & 1u, (low >> ch) & 1u));
      }
    }
  }
  return err;
}

/* ---- IO contract ---- */

OP_DECLARE_READ(CONTRACT_IO, OP_VOLTAGE) {
  ERR_TRY(sys_io_pins_check(&c->pins, c->id, args->pin));
  uint16_t code = 0;
  ERR_TRY(chip_read_code(c, args->pin, &code));
  return CMD_REPLY(CONTRACT_IO, OP_VOLTAGE, .pin = args->pin, .voltage_mV = code_to_mV(c, code));
}

// Every channel is an analog input: ADC is the one mode.
OP_DECLARE_WRITE(CONTRACT_IO, OP_MODE) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  ERR_CHECK(args->mode == SYS_IO_MODE_ADC, ERR_IO_MODE_UNSUPPORTED, .id = c->id, .pin = args->pin, .mode = args->mode);
  return NULL;
}

static err_h disarm(ads_t* c, uint8_t ch) {
  c->armed &= (uint8_t)~(1u << ch);
  c->past &= (uint8_t)~(1u << ch);
  c->entry[ch] = k_off;
  return set_window(c, ch, &k_off);
}

/*******************************************************************************
 * CONTRACT_IO : OP_INTR (write)
 * The window comparator is an analog input's only trigger. threshold_up_mV 0:
 * no high limit.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_INTR) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  const uint8_t mode = args->cfg.mode;
  if (mode == SYS_IO_INTR_DISABLE) {
    return disarm(c, args->pin);
  }
  ERR_CHECK(mode == SYS_IO_INTR_ADC_WINDOW_OUTSIDE || mode == SYS_IO_INTR_ADC_WINDOW_INSIDE,
            ERR_IO_FEATURE_UNSUPPORTED, .id = c->id, .pin = args->pin);
  const sys_io_adc_window_t* a = &args->cfg.adc;
  const uint16_t up_mV = a->threshold_up_mV == 0 ? c->full_scale_mV : a->threshold_up_mV;
  ERR_CHECK_RANGE(up_mV, 0, c->full_scale_mV);
  ERR_CHECK_RANGE(a->threshold_down_mV, 0, up_mV);
  ERR_CHECK_RANGE(a->hysteresis_mV, 0, code_to_mV(c, HYSTERESIS_MAX * HYSTERESIS_STEP));
  ERR_CHECK_RANGE(a->event_counter_threshold, 0, EVENT_COUNT_MAX);
  const uint16_t upper = mV_to_code(c, up_mV);
  const uint16_t lower = mV_to_code(c, a->threshold_down_mV);
  const uint32_t hyst = mV_to_code(c, a->hysteresis_mV) / HYSTERESIS_STEP;
  const bool inside = mode == SYS_IO_INTR_ADC_WINDOW_INSIDE;
  const window_t w = {
      .high_th = inside ? lower : upper,
      .low_th = inside ? upper : lower,
      .hysteresis = (uint8_t)(hyst > HYSTERESIS_MAX ? HYSTERESIS_MAX : hyst),
      .event_count = (uint8_t)(a->event_counter_threshold <= 1 ? 0 : a->event_counter_threshold - 1),
      .in_band = inside,
  };
  c->entry[args->pin] = w;
  c->armed |= (uint8_t)(1u << args->pin);
  c->past &= (uint8_t)~(1u << args->pin);
  return set_window(c, args->pin, &w);
}

// The channel's comparator off.
OP_DECLARE_WRITE(CONTRACT_IO, OP_RESET) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  return disarm(c, args->pin);
}

SYS_IO_PINS_HANDLERS

/* ---- Lifecycle ---- */

/*******************************************************************************
 * CONTRACT_SYS_DEVICE : OP_RESET (write)
 * The chip's defaults: every comparator off.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  return chip_reset(c);
}

// The sequencer stops: nothing converts, no alert. The windows are kept for resume.
static err_h suspend(ads_t* c) {
  return chip_write(c, REG_SEQUENCE_CFG, 0);
}

// Every channel back on its entry watch.
static err_h resume(ads_t* c) {
  memcpy(c->live, c->entry, sizeof(c->live));
  c->past = 0;
  return chip_restore(c);
}

static err_h create(uint8_t id, ads_t* c, const ads7128_cfg_t* cfg) {
  ERR_CHECK_RANGE(cfg->i2c_addr, 0x10, 0x17);
  ERR_CHECK_RANGE(cfg->full_scale_mV, 1, UINT16_MAX);
  c->id = id;
  c->full_scale_mV = cfg->full_scale_mV;
  sys_io_pins_init(&c->pins, ADS7128_CHANNELS);
  ERR_TRY(sys_i2c_dev_attach(id, &c->i2c, cfg->i2c_bus, cfg->i2c_addr, I2C_SPEED_HZ));
  ERR_TRY(chip_reset(c));
  // ALERT: active low, its fall reaches OP_ALERT (inline: I2C reads, no delay).
  return sys_io_claim_input(id, &c->intr, cfg->intr_pin, IO_EV_FALLING, OWN_ADS7128_OP_ALERT,
                            SYS_IO_INTR_MODE_FALLING_EDGE);
}

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),
    OP_ENTRY_R(CONTRACT_IO, OP_VOLTAGE),  OP_ENTRY_W(CONTRACT_IO, OP_MODE),  OP_ENTRY_W(CONTRACT_IO, OP_INTR),
    OP_ENTRY_W(CONTRACT_IO, OP_RESET),
    SYS_IO_PINS_ENTRIES,
    OP_ENTRY_W(OWN_ADS7128, OP_ALERT),
};

SYS_DEVICE_TYPE_DEFINE(ADS7128, ads7128_cfg_t, create, suspend, resume, k_ops)
