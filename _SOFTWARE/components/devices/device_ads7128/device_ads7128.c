#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "device_ads7128.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_io.h"

#define OWNER OWNER_DEVICE

#define ADS7128_I2C_FREQUENCY 100000
#define ADS7128_CH_COUNT 8
#define ADS7128_CH_MASK_ALL 0xFF
#define ADS7128_MAX_CODE 0x0FFF

#define PINS_COUNT ADS7128_CH_COUNT
#define PINS_MASK ADS7128_CH_MASK_ALL

/* Time the chip needs after a software reset before it answers again */
#define ADS7128_RESET_DELAY_MS 5

/* A conversion frame issued right after a channel change can still carry the
 * previous channel, so a manual read retries until the appended channel ID
 * matches what was asked for. */
#define ADS7128_MANUAL_READ_TRIES 3

/* ~1 kSPS on the low-power oscillator: fast enough for threshold monitoring,
 * slow enough to keep the autonomous sequencer out of the way of I2C traffic */
#define ADS7128_CLK_DIV 0x0A

/* Hysteresis is a 4-bit field applied as [hysteresis, 000b], i.e. in steps of 8 codes */
#define ADS_HYSTERESIS_STEP 8

/* Frame opcodes: [opcode][register][data...] */
#define OP_SINGLE_REGISTER_WRITE 0x08
#define OP_CONTINUOUS_REGISTER_WRITE 0x28
#define OP_CONTINUOUS_REGISTER_READ 0x30

/* Registers (datasheet 8.6) and the bits used here */
#define REG_GENERAL_CFG 0x01
#define REG_DATA_CFG 0x02
#define REG_OSR_CFG 0x03
#define REG_OPMODE_CFG 0x04
#define REG_PIN_CFG 0x05
#define REG_SEQUENCE_CFG 0x10
#define REG_MANUAL_CH_SEL 0x11
#define REG_AUTO_SEQ_CH_SEL 0x12
#define REG_ALERT_CH_SEL 0x14
#define REG_ALERT_PIN_CFG 0x17
#define REG_EVENT_FLAG 0x18
#define REG_EVENT_HIGH_FLAG 0x1A
#define REG_EVENT_LOW_FLAG 0x1C
#define REG_EVENT_RGN 0x1E
#define REG_HYSTERESIS_CH0 0x20  // four registers per channel: HYSTERESIS, HIGH_TH, EVENT_COUNT, LOW_TH
#define REG_RECENT_CH0_LSB 0xA0  // two registers per channel

#define GENERAL_RST 0x01
#define GENERAL_DWC_EN 0x10    // digital window comparator on
#define GENERAL_STATS_EN 0x20  // keeps RECENT_CHx updated
#define DATA_APPEND_CHID 0x10  // channel ID appended to conversion data
#define OPMODE_CONV_AUTO 0x20
#define OPMODE_OSC_LOW_POWER 0x10
#define SEQ_START 0x10
#define SEQ_MODE_AUTO 0x01
// OSR_CFG = 0 (no averaging), PIN_CFG = 0 (every channel an analog input), ALERT_PIN_CFG = 0 (open drain, active low)

/* Region watched by the digital window comparator (EVENT_RGN bit of a channel) */
typedef enum {
  ALERT_OUT_OF_BAND = 0,  // flag set below the low or above the high threshold
  ALERT_IN_BAND = 1,      // flag set while high_th < code < low_th (bounds swap registers in-band; verified on the board)
} alert_region_e;

/* Window comparator settings of a single channel. Thresholds are raw 12-bit
 * codes; hysteresis and event_count are the raw 4-bit register fields. */
typedef struct {
  bool enabled;         // route this channel to the ALERT pin (ALERT_CH_SEL)
  uint16_t high_th;     // 12-bit high threshold, 0xFFF disables the high side
  uint16_t low_th;      // 12-bit low threshold, 0x000 disables the low side
  uint8_t hysteresis;   // 4-bit, applied as [hysteresis, 000b] on both thresholds
  uint8_t event_count;  // 4-bit, alert after n+1 consecutive violations
  alert_region_e region;
} alert_cfg_t;

// Install steps, recorded so teardown rolls back only what was actually built
enum { ADS_STEP_I2C_ADDED = 0, ADS_STEP_INTR_READY = 1, ADS_STEP_INTR_SUB = 2 };

// Instance state: the whole device in one struct (chip shadow, pins, bus; the create cfg is not kept).
typedef struct ads_ctx_t {
  sys_device_base_t base;  // must be first
  sys_i2c_dev_t i2c;
  sys_io_pin_ref_t intr_pin;  // the ALERT pin of the create cfg, converted once at install
  float mv_per_code;

  /* Shadow of the chip configuration, replayed by chip_restore() */
  uint8_t alert_ch_mask;  // ALERT_CH_SEL: channels driving the ALERT pin
  uint8_t event_rgn;      // EVENT_RGN: 1 = in-band alert for that channel
  alert_cfg_t alerts[ADS7128_CH_COUNT];
  bool autonomous;                          // sequencer runs on its own (any alert armed)
  uint16_t recent_codes[ADS7128_CH_COUNT];  // last 12-bit code read per channel

  sys_io_intr_mode_e intr_modes[PINS_COUNT];
  uint8_t intr_sub; /* sys_event subscription on intr_pin */
  // The user-facing config from sys_io_configure_intr(), remembered so
  // handle_alert can restore it once a crossing has been reported and
  // recovery has been detected (see entry/exit watch swap below).
  alert_cfg_t entry_alert_cfg[PINS_COUNT];
  // Bit N set = channel N is currently past its threshold and has already
  // been reported; the chip is presently armed with an "exit watch" (see
  // arm_exit_watch()) instead of its normal entry config. The chip's
  // EVENT_HIGH_FLAG/EVENT_LOW_FLAG has no notion of "newly" violating vs
  // "still" violating - a signal parked past the threshold re-latches it
  // every autonomous sample (~every 8ms here). Rather than poll for recovery,
  // handle_alert flips the armed config between the entry threshold
  // (waiting to cross) and a hysteresis-retreated exit threshold (waiting to
  // recover) so the chip itself only ever raises ALERT on a genuine
  // transition in either direction - no timer, no per-cycle spam.
  uint8_t alert_active_mask;
} ads_ctx_t;

/* ---- Chip access: err_h, the dispatcher above adds the device id ---- */

static SE_MUST_USE err_h chip_write_reg(ads_ctx_t* c, uint8_t reg, uint8_t val) {
  const uint8_t frame[3] = {OP_SINGLE_REGISTER_WRITE, reg, val};
  return sys_i2c_write(&c->i2c, frame, sizeof(frame));
}

static SE_MUST_USE err_h chip_write_block(ads_ctx_t* c, uint8_t first_reg, const uint8_t* data, size_t len) {
  uint8_t frame[2 + 4];
  SE_CHECK_IN_RANGE((uint32_t)len, 1, sizeof(frame) - 2);
  frame[0] = OP_CONTINUOUS_REGISTER_WRITE;
  frame[1] = first_reg;
  memcpy(&frame[2], data, len);
  return sys_i2c_write(&c->i2c, frame, len + 2);
}

static SE_MUST_USE err_h chip_read_block(ads_ctx_t* c, uint8_t first_reg, uint8_t* data, size_t len) {
  const uint8_t frame[2] = {OP_CONTINUOUS_REGISTER_READ, first_reg};
  return sys_i2c_write_read(&c->i2c, frame, sizeof(frame), data, len);
}

static void load_defaults(ads_ctx_t* c) {
  c->alert_ch_mask = 0;
  c->event_rgn = 0;
  c->autonomous = false;
  for (uint8_t ch = 0; ch < ADS7128_CH_COUNT; ch++) {
    c->alerts[ch] = (alert_cfg_t){.enabled = false, .high_th = ADS7128_MAX_CODE, .low_th = 0, .hysteresis = 0, .event_count = 0, .region = ALERT_OUT_OF_BAND};
  }
  memset(c->recent_codes, 0, sizeof(c->recent_codes));
}

static uint8_t opmode_value(bool autonomous) {
  return (autonomous ? OPMODE_CONV_AUTO : 0) | OPMODE_OSC_LOW_POWER | ADS7128_CLK_DIV;
}

/* Stop the sequencer. The datasheet requires an idle sequencer before the mode
 * or any channel configuration is changed. */
static SE_MUST_USE err_h chip_stop_sequence(ads_ctx_t* c) { return chip_write_reg(c, REG_SEQUENCE_CFG, 0); }

static SE_MUST_USE err_h chip_write_alert_block(ads_ctx_t* c, uint8_t channel) {
  const alert_cfg_t* alert = &c->alerts[channel];

  /* Thresholds are split across the block: the MSB register holds bits [11:4],
   * the upper nibble of the neighbouring register holds bits [3:0]. */
  const uint8_t block[4] = {
      (uint8_t)(((alert->high_th & 0x0F) << 4) | (alert->hysteresis & 0x0F)),  // HYSTERESIS_CHx
      (uint8_t)((alert->high_th >> 4) & 0xFF),                                 // HIGH_TH_CHx
      (uint8_t)(((alert->low_th & 0x0F) << 4) | (alert->event_count & 0x0F)),  // EVENT_COUNT_CHx
      (uint8_t)((alert->low_th >> 4) & 0xFF),                                  // LOW_TH_CHx
  };
  return chip_write_block(c, REG_HYSTERESIS_CH0 + channel * 4, block, sizeof(block));
}

/* EVENT_FLAG (0x18) is a read-only summary of EVENT_HIGH_FLAG/EVENT_LOW_FLAG
 * (datasheet 8.6.18) - clearing only ever needs the two writes below. */
static SE_MUST_USE err_h chip_clear_event_flags(ads_ctx_t* c, uint8_t high_mask, uint8_t low_mask) {
  if (high_mask) SE_TRY(chip_write_reg(c, REG_EVENT_HIGH_FLAG, high_mask));
  if (low_mask) SE_TRY(chip_write_reg(c, REG_EVENT_LOW_FLAG, low_mask));
  return NULL;
}

/* The window comparator only sees codes the ADC actually converts. As long as a
 * channel is armed the chip therefore runs the sequencer on its own clock; with
 * no alert armed it falls back to manual, host-triggered conversions. */
static SE_MUST_USE err_h chip_apply_conv_mode(ads_ctx_t* c) {
  const bool autonomous = (c->alert_ch_mask != 0);

  SE_TRY(chip_stop_sequence(c));

  if (autonomous) {
    SE_TRY(chip_write_reg(c, REG_AUTO_SEQ_CH_SEL, ADS7128_CH_MASK_ALL));
    SE_TRY(chip_write_reg(c, REG_OPMODE_CFG, opmode_value(true)));
    /* STATS_EN keeps RECENT_CHx updated, which is the only way to read a channel
     * while the device drives the sequence itself. */
    SE_TRY(chip_write_reg(c, REG_GENERAL_CFG, GENERAL_DWC_EN | GENERAL_STATS_EN));
    SE_TRY(chip_write_reg(c, REG_SEQUENCE_CFG, SEQ_START | SEQ_MODE_AUTO));
  } else {
    SE_TRY(chip_write_reg(c, REG_GENERAL_CFG, 0));
    SE_TRY(chip_write_reg(c, REG_OPMODE_CFG, opmode_value(false)));
  }

  c->autonomous = autonomous;
  return NULL;
}

/* Replays the shadow configuration (install, reset, resume). */
static SE_MUST_USE err_h chip_restore(ads_ctx_t* c) {
  SE_TRY(chip_stop_sequence(c));

  /* Appending the channel ID to conversion data lets a manual read prove which
   * channel it actually got. */
  SE_TRY(chip_write_reg(c, REG_DATA_CFG, DATA_APPEND_CHID));
  SE_TRY(chip_write_reg(c, REG_OSR_CFG, 0));
  SE_TRY(chip_write_reg(c, REG_PIN_CFG, 0));
  SE_TRY(chip_write_reg(c, REG_ALERT_PIN_CFG, 0));

  for (uint8_t ch = 0; ch < ADS7128_CH_COUNT; ch++) {
    SE_TRY(chip_write_alert_block(c, ch));
  }

  SE_TRY(chip_write_reg(c, REG_EVENT_RGN, c->event_rgn));
  SE_TRY(chip_write_reg(c, REG_ALERT_CH_SEL, c->alert_ch_mask));
  SE_TRY(chip_clear_event_flags(c, 0xFF, 0xFF));

  return chip_apply_conv_mode(c);
}

static SE_MUST_USE err_h chip_reset(ads_ctx_t* c) {
  SE_TRY(chip_write_reg(c, REG_GENERAL_CFG, GENERAL_RST));
  vTaskDelay(pdMS_TO_TICKS(ADS7128_RESET_DELAY_MS));

  load_defaults(c);
  return chip_restore(c);
}

static SE_MUST_USE err_h chip_set_alert_cfg(ads_ctx_t* c, uint8_t channel, const alert_cfg_t* cfg) {
  c->alerts[channel] = *cfg;

  const uint8_t ch_bit = (uint8_t)(1u << channel);
  if (cfg->enabled) {
    c->alert_ch_mask |= ch_bit;
  } else {
    c->alert_ch_mask &= (uint8_t)~ch_bit;
  }
  if (cfg->region == ALERT_IN_BAND) {
    c->event_rgn |= ch_bit;
  } else {
    c->event_rgn &= (uint8_t)~ch_bit;
  }

  SE_TRY(chip_stop_sequence(c));
  SE_TRY(chip_write_alert_block(c, channel));
  SE_TRY(chip_write_reg(c, REG_EVENT_RGN, c->event_rgn));
  SE_TRY(chip_write_reg(c, REG_ALERT_CH_SEL, c->alert_ch_mask));
  /* A flag left over from the previous thresholds would hold ALERT asserted */
  SE_TRY(chip_clear_event_flags(c, ch_bit, ch_bit));

  return chip_apply_conv_mode(c);
}

static SE_MUST_USE err_h chip_clear_alert_cfg(ads_ctx_t* c, uint8_t channel) {
  const alert_cfg_t cleared = {.enabled = false, .high_th = ADS7128_MAX_CODE, .low_th = 0, .hysteresis = 0, .event_count = 0, .region = ALERT_OUT_OF_BAND};
  return chip_set_alert_cfg(c, channel, &cleared);
}

/* Manual mode: the write frame moves the mux, the read frame that follows starts
 * the conversion and returns [D11:D4][D3:D0, CHID]. */
static SE_MUST_USE err_h chip_manual_read(ads_ctx_t* c, uint8_t channel, uint16_t* out_code) {
  const uint8_t tx[3] = {OP_SINGLE_REGISTER_WRITE, REG_MANUAL_CH_SEL, (uint8_t)(channel & 0x0F)};
  uint8_t rx[2] = {0};

  for (uint8_t attempt = 0; attempt < ADS7128_MANUAL_READ_TRIES; attempt++) {
    SE_TRY(sys_i2c_write_read(&c->i2c, tx, sizeof(tx), rx, sizeof(rx)));
    if ((rx[1] & 0x0F) == channel) {
      *out_code = (uint16_t)(((((uint16_t)rx[0]) << 8) | rx[1]) >> 4);
      return NULL;
    }
  }
  SE_FAIL(ERR_ESP_ERR, .esp_code = ESP_ERR_INVALID_RESPONSE);
}

/* Autonomous mode: the sequencer owns the mux, so the last conversion result is
 * taken from the statistics block instead of from a conversion frame. */
static SE_MUST_USE err_h chip_recent_read(ads_ctx_t* c, uint8_t channel, uint16_t* out_code) {
  uint8_t raw[2] = {0};
  SE_TRY(chip_read_block(c, REG_RECENT_CH0_LSB + channel * 2, raw, sizeof(raw)));

  /* LSB register first, result is MSB aligned in 16 bits */
  *out_code = (uint16_t)(((((uint16_t)raw[1]) << 8) | raw[0]) >> 4);
  return NULL;
}

static SE_MUST_USE err_h chip_read_channel(ads_ctx_t* c, uint8_t channel, uint16_t* out_code) {
  uint16_t code = 0;
  SE_TRY(c->autonomous ? chip_recent_read(c, channel, &code) : chip_manual_read(c, channel, &code));
  c->recent_codes[channel] = code;
  *out_code = code;
  return NULL;
}

/* One block read covers EVENT_FLAG (0x18) through EVENT_LOW_FLAG (0x1C) */
static SE_MUST_USE err_h chip_get_event_flags(ads_ctx_t* c, uint8_t* high, uint8_t* low) {
  uint8_t raw[REG_EVENT_LOW_FLAG - REG_EVENT_FLAG + 1] = {0};
  SE_TRY(chip_read_block(c, REG_EVENT_FLAG, raw, sizeof(raw)));
  *high = raw[REG_EVENT_HIGH_FLAG - REG_EVENT_FLAG];
  *low = raw[REG_EVENT_LOW_FLAG - REG_EVENT_FLAG];
  return NULL;
}

/* ---- Scaling ---- */

static inline uint32_t code_to_mV(const ads_ctx_t* c, uint16_t code) { return (uint32_t)((float)code * c->mv_per_code + 0.5f); }

static inline uint16_t mv_to_code(const ads_ctx_t* c, uint32_t mv) {
  uint32_t code = (uint32_t)((float)mv / c->mv_per_code + 0.5f);
  return (code > ADS7128_MAX_CODE) ? ADS7128_MAX_CODE : (uint16_t)code;
}

static inline uint8_t hysteresis_field(const ads_ctx_t* c, uint32_t hysteresis_mV) {
  uint16_t steps = (uint16_t)(mv_to_code(c, hysteresis_mV) / ADS_HYSTERESIS_STEP);
  return (steps > 0x0F) ? 0x0F : (uint8_t)steps;
}

/* The register field counts n+1 consecutive violations, so a threshold of 0 or 1
   means "alert on the first sample". */
static inline uint8_t event_count_field(uint16_t event_counter_threshold) {
  if (event_counter_threshold <= 1) return 0;
  uint16_t field = (uint16_t)(event_counter_threshold - 1);
  return (field > 0x0F) ? 0x0F : (uint8_t)field;
}

/* ---- Alert dispatcher ---- */

/* Runs in the callback task, hooked to the ALERT pin of the chip. The chip only
   says "something crossed a threshold", so the flags decide which channels fired
   and the flags are cleared afterwards to release ALERT for the next event.

   ALERT is the live OR of the EVENT_FLAG bits (datasheet 8.3.11), and each
   flag is latched - set on a violation, and NOT self-clearing when the
   signal returns in range; only an explicit write-1 clears it. GPIO42's ESP32
   interrupt is edge-triggered (falling edge only), so if the sequencer
   re-latches a flag between our read and our clear (a real risk: it keeps
   converting autonomously the whole time this handler is running), a flag
   can be left set with no further edge ever able to fire and revisit it -
   ALERT just stays asserted forever, desynced from reality. Looping here
   until a poll comes back with nothing pending closes that gap.

   The flag is also silent about *repetition*: a signal parked past the
   threshold re-latches it on every autonomous sample (~every 8ms here with
   8 channels enabled), not just the first time it crossed. The chip has no
   concept of "newly" vs "still" violating, and no signal at all for "back in
   range" - so getting one notification per real crossing needs something
   watching for recovery too, and that has to be the chip itself (a CPU-side
   poll/timer would mean waking up constantly just to check). arm_exit_watch()
   is that trick: once a crossing fires, instead of re-arming the same
   threshold (which just re-trips every cycle for as long as the signal stays
   past it), the channel gets reconfigured to watch for the *opposite*
   condition - the signal retreating back past (threshold -/+ hysteresis).
   That watch stays genuinely quiet the whole time the signal remains past
   the original threshold, since its own condition simply isn't true yet.
   Only a real recovery re-arms the original entry watch. Net effect: exactly
   one ALERT edge per genuine transition in either direction, entirely
   chip-driven, no polling. */
/* The exit watch is always OUT_OF_BAND. After an out-of-band crossing it is
 * single-sided (one threshold disabled = "watch this one side only"); after
 * an in-band entry it watches both sides of the band, since leaving it either
 * way is the recovery. */
static void arm_exit_watch(alert_cfg_t* out, const alert_cfg_t* entry, bool high_fired, bool low_fired) {
  *out = (alert_cfg_t){
      .enabled = true,
      .high_th = ADS7128_MAX_CODE,  // disabled unless low_fired narrows it below
      .low_th = 0,                  // disabled unless high_fired narrows it above
      .hysteresis = 0,
      .event_count = entry->event_count,
      .region = ALERT_OUT_OF_BAND,
  };
  uint16_t hyst_codes = (uint16_t)entry->hysteresis * ADS_HYSTERESIS_STEP;
  if (entry->region == ALERT_IN_BAND) {
    // Was inside (entry->high_th, entry->low_th) - the in-band register swap;
    // recovered once it leaves the band on either side, past the hysteresis.
    out->low_th = (entry->high_th > hyst_codes) ? (uint16_t)(entry->high_th - hyst_codes) : 0;
    uint32_t above = (uint32_t)entry->low_th + hyst_codes;
    out->high_th = (above > ADS7128_MAX_CODE) ? ADS7128_MAX_CODE : (uint16_t)above;
    return;
  }
  if (high_fired) {
    // Was above entry->high_th; recovered once it drops back below (high_th - hysteresis).
    out->low_th = (entry->high_th > hyst_codes) ? (uint16_t)(entry->high_th - hyst_codes) : 0;
  }
  if (low_fired) {
    // Was below entry->low_th; recovered once it rises back above (low_th + hysteresis).
    uint32_t retreat = (uint32_t)entry->low_th + hyst_codes;
    out->high_th = (retreat > ADS7128_MAX_CODE) ? ADS7128_MAX_CODE : (uint16_t)retreat;
  }
}

/* Publishes each new window crossing. */
static SE_MUST_USE err_h handle_alert(const sys_event_t* event, ads_ctx_t* c) {
  err_h err = NULL;

  for (int guard = 0; guard < 16; guard++) {
    uint8_t high, low;
    SE_TRY(chip_get_event_flags(c, &high, &low));

    uint8_t pending = (uint8_t)(high | low);
    if (!pending) break;

    for (uint8_t pin = 0; pin < PINS_COUNT; pin++) {
      if (!(pending & (1u << pin))) continue;

      sys_io_intr_mode_e mode = c->intr_modes[pin];
      if (mode == SYS_IO_INTR_DISABLE) continue;

      uint16_t code = 0;
      err_h read_err = chip_read_channel(c, pin, &code);
      if (read_err) {
        SE_release(read_err);
        code = c->recent_codes[pin];  // report the last good reading rather than nothing
      }

      bool was_active = (c->alert_active_mask & (1u << pin)) != 0;
      if (!was_active) {
        // Entry watch tripped: a genuine new crossing.
        c->alert_active_mask |= (uint8_t)(1u << pin);
        SYS_DEV_TEARDOWN_STEP(err, sys_io_publish(SYS_DEV_GET_ID(c), pin, mode, (int32_t)code_to_mV(c, code), SYS_EVENT_CAUSED_BY(event)));
        alert_cfg_t exit_cfg;
        arm_exit_watch(&exit_cfg, &c->entry_alert_cfg[pin], (high & (1u << pin)) != 0, (low & (1u << pin)) != 0);
        SE_TRY(chip_set_alert_cfg(c, pin, &exit_cfg));
      } else {
        // Exit watch tripped: genuinely recovered - re-arm the original watch.
        c->alert_active_mask &= (uint8_t)~(1u << pin);
        SE_TRY(chip_set_alert_cfg(c, pin, &c->entry_alert_cfg[pin]));
      }
    }

    SE_TRY(chip_clear_event_flags(c, high, low));
  }
  return err;
}

/* Inline listener of intr_pin. No dispatcher wraps a listener's error, so it carries the device id here. */
static SE_MUST_USE err_h device_event_handler(const sys_event_t* event, void* handle) {
  SYS_DEV_CTX_FROM(ads_ctx_t, c, handle);
  err_h err = handle_alert(event, c);
  return SYS_DEV_WRAP(err, SYS_DEV_GET_ID(c));
}

/* ---- IO contract ---- */

static SE_MUST_USE err_h io_get_voltage(void* handle, sys_io_pin_num_t pin, int32_t* out_mV) {
  SYS_DEV_CTX_FROM(ads_ctx_t, c, handle);
  SE_CHECK_NOT_NULL(out_mV);
  VERIFY_PIN(SYS_DEV_GET_ID(c), pin, PINS_MASK);

  uint16_t code = 0;
  SE_TRY(chip_read_channel(c, pin, &code));
  *out_mV = (int32_t)code_to_mV(c, code);
  return NULL;
}

/* Channels are analog inputs out of reset and this device exposes nothing else,
   so the only mode that can be honoured is ADC. */
static SE_MUST_USE err_h io_set_mode(void* handle, sys_io_pin_num_t pin, sys_io_mode_e mode) {
  SYS_DEV_CTX_FROM(ads_ctx_t, c, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(c), pin, PINS_MASK);

  if (mode != SYS_IO_MODE_ADC) {
    SE_FAIL(ERR_IO_PIN_MODE_UNSUPPORTED, SYS_DEV_GET_ID(c), pin, mode);
  }
  return NULL;
}

static SE_MUST_USE err_h io_configure_intr(void* handle, sys_io_pin_num_t pin, const sys_io_intr_config_t* config) {
  SYS_DEV_CTX_FROM(ads_ctx_t, c, handle);
  SE_CHECK_NOT_NULL(config);
  VERIFY_PIN(SYS_DEV_GET_ID(c), pin, PINS_MASK);

  if (config->mode == SYS_IO_INTR_DISABLE) {
    c->intr_modes[pin] = SYS_IO_INTR_DISABLE;
    c->alert_active_mask &= (uint8_t)~(1u << pin);
    return chip_clear_alert_cfg(c, pin);
  }

  /* The on-chip window comparator is the only trigger an analog input has;
     edge modes belong to digital pins. */
  if (config->mode != SYS_IO_INTR_ADC_WINDOW_INSIDE && config->mode != SYS_IO_INTR_ADC_WINDOW_OUTSIDE) {
    SE_FAIL(ERR_IO_PIN_FEATURE_UNSUPPORTED, SYS_DEV_GET_ID(c), pin);
  }

  /* 0 mV up means "no high limit": full scale never trips the comparator */
  uint16_t upper = (config->adc.adc_threshold_up_mV == 0) ? ADS7128_MAX_CODE : mv_to_code(c, config->adc.adc_threshold_up_mV);
  uint16_t lower = mv_to_code(c, config->adc.adc_threshold_down_mV);
  bool inside = config->mode == SYS_IO_INTR_ADC_WINDOW_INSIDE;
  alert_cfg_t alert = {
      .enabled = true,
      /* In-band (EVENT_RGN = 1) the chip flags HIGH_TH < code < LOW_TH, so the
         bounds swap registers (verified on the board 2026-09-24; datasheet
         8.6.21 misprints it as "low threshold > result < high threshold"). */
      .high_th = inside ? lower : upper,
      .low_th = inside ? upper : lower,
      .hysteresis = hysteresis_field(c, config->adc.adc_threshold_hysteresis_mV),
      .event_count = event_count_field(config->adc.adc_event_counter_threshold),
      .region = inside ? ALERT_IN_BAND : ALERT_OUT_OF_BAND,
  };

  SE_TRY(chip_set_alert_cfg(c, pin, &alert));

  c->intr_modes[pin] = config->mode;
  // Remembered so handle_alert can restore this exact watch after a
  // crossing has been reported and recovery detected (see arm_exit_watch).
  c->entry_alert_cfg[pin] = alert;
  // Fresh config, no crossing reported yet - let the next real violation
  // dispatch instead of carrying over whatever a previous config left behind.
  c->alert_active_mask &= (uint8_t)~(1u << pin);
  return NULL;
}

static SE_MUST_USE err_h io_reset_pin(void* handle, sys_io_pin_num_t pin) {
  SYS_DEV_CTX_FROM(ads_ctx_t, c, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(c), pin, PINS_MASK);

  c->intr_modes[pin] = SYS_IO_INTR_DISABLE;
  return chip_clear_alert_cfg(c, pin);
}

static const sys_io_contract_t s_ads7128_io_contract = {
    .reset = io_reset_pin, .set_mode = io_set_mode, .configure_intr = io_configure_intr, .get_voltage = io_get_voltage};

/* ---- Lifecycle ---- */

// Doubles as the install rollback path: each step is gated on having actually
// run, and no step may early-return - teardown must always free everything.
static SE_MUST_USE err_h device_uninstall(void* handle) {
  SYS_DEV_CTX_FROM(ads_ctx_t, c, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(c, ADS_STEP_INTR_SUB) {
    SYS_DEV_TEARDOWN_STEP(err, sys_event_unsubscribe(c->intr_sub, false));
  }
  IF_SYS_DEV_STEP_DONE(c, ADS_STEP_INTR_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(c->intr_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->intr_pin));
  }
  IF_SYS_DEV_STEP_DONE(c, ADS_STEP_I2C_ADDED) {
    SYS_DEV_TEARDOWN_STEP(err, sys_i2c_dev_remove(&c->i2c));
  }

  free(c);
  return err;
}

static SE_MUST_USE err_h device_reset(void* handle) {
  SYS_DEV_CTX_FROM(ads_ctx_t, c, handle);
  for (uint8_t pin = 0; pin < PINS_COUNT; pin++) {
    c->intr_modes[pin] = SYS_IO_INTR_DISABLE;
  }
  return chip_reset(c);
}

/* The chip has no shutdown state: it simply stops converting once the sequencer
   is idle, which is what a manual-mode configuration already gives. */
static SE_MUST_USE err_h device_suspend(void* handle) {
  (void)handle;
  return NULL;
}

static SE_MUST_USE err_h device_resume(void* handle) {
  SYS_DEV_CTX_FROM(ads_ctx_t, c, handle);
  return chip_restore(c);
}

static SE_MUST_USE err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_ads7128_cfg_t* cfg = (const d_ads7128_cfg_t*)cfg_blob;

  // Every reading and every threshold is scaled by this, so it may not be zero
  if (cfg->vref_mV == 0) {
    SE_FAIL(ERR_INVALID_VAL_UI32, .val = 0, .min = 1, .max = UINT32_MAX);
  }

  // The cfg is read only here: what the device keeps (the ALERT pin, the scale) goes into its own state.
  ads_ctx_t* c = (ads_ctx_t*)calloc(1, sizeof(ads_ctx_t));
  SE_CHECK_IF_ALLOCATED(c);
  c->base.device_id = cfg->device_id;
  c->intr_pin = cfg->intr_pin;
  c->mv_per_code = (float)cfg->vref_mV / (float)ADS7128_MAX_CODE;
  load_defaults(c);
  err_h err = NULL;

  sys_i2c_dev_init(&c->i2c, cfg->i2c_bus != 0, cfg->i2c_addr, ADS7128_I2C_FREQUENCY);
  SYS_DEV_INSTALL_STEP_BIT(c, ADS_STEP_I2C_ADDED, sys_i2c_dev_add(&c->i2c), "i2c add (probes the chip)");

  SYS_DEV_INSTALL_STEP(chip_reset(c), "chip start");

  if (sys_io_pin_is_valid(c->intr_pin)) {
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(c->intr_pin), "intr pin mode");
    SYS_DEV_INSTALL_STEP_BIT(c, ADS_STEP_INTR_SUB, sys_io_subscribe_pin(c->intr_pin, device_event_handler, c, &c->intr_sub), "intr pin subscribe");
    // ALERT is active low, so the falling edge is the assertion
    sys_io_intr_config_t intr_cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE};
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(c->intr_pin, &intr_cfg), "intr pin configure");
    SYS_DEV_INSTALL_STEP_BIT(c, ADS_STEP_INTR_READY, sys_io_lock_pin(c->intr_pin), "intr pin lock");
  }

  *out_device_handle = c;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, out_device_handle, device_uninstall, c);
  return NULL;
}

static const uint8_t s_pin_refs[] = {offsetof(d_ads7128_cfg_t, intr_pin)};

const sys_device_class_t g_ads7128_class = {
    .type_id = ADS7128_TYPE_ID,
    .cfg_size = sizeof(d_ads7128_cfg_t),
    SYS_DEVICE_PINS(s_pin_refs),
    .name = "ADS7128_ADC",
    .contracts = {[SYS_DEVICE_CONTRACT_IO] = &s_ads7128_io_contract},
    .ops = {.install = device_install,
        .uninstall = device_uninstall,
        .reset = device_reset,
        .suspend = device_suspend,
        .resume = device_resume},
};
