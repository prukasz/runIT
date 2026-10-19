#include "device_ads7128.h"

#include "sys_event.h"
#include "sys_i2c.h"
#include "sys_pins.h"
#include "utils.h"

#define I2C_SPEED_HZ 100000u
#define CH_MASK_ALL 0xFFu
#define MAX_CODE 0x0FFFu

// Time the chip needs after a software reset before it answers again (a bounded wait, like the TPS55289's EN).
#define RESET_DELAY_MS 5u

/* A conversion frame issued right after a channel change can still carry the previous channel, so a manual read
   retries until the appended channel ID matches what was asked for. */
#define MANUAL_READ_TRIES 3u

/* ~1 kSPS on the low-power oscillator: fast enough for threshold monitoring, slow enough to keep the autonomous
   sequencer out of the way of I2C traffic. */
#define CLK_DIV 0x0Au

// Hysteresis is a 4-bit field applied as [hysteresis, 000b], i.e. in steps of 8 codes.
#define HYSTERESIS_STEP 8u
#define HYSTERESIS_FIELD_MAX 0x0Fu
#define EVENT_COUNT_MAX 16u  // the 4-bit field counts n + 1 violations

// Frame opcodes: [opcode][register][data...]
#define OP_SINGLE_REGISTER_WRITE 0x08u
#define OP_CONTINUOUS_REGISTER_WRITE 0x28u
#define OP_CONTINUOUS_REGISTER_READ 0x30u

// Registers (datasheet 8.6) and the bits used here
#define REG_GENERAL_CFG 0x01u
#define REG_DATA_CFG 0x02u
#define REG_OSR_CFG 0x03u
#define REG_OPMODE_CFG 0x04u
#define REG_PIN_CFG 0x05u
#define REG_SEQUENCE_CFG 0x10u
#define REG_MANUAL_CH_SEL 0x11u
#define REG_AUTO_SEQ_CH_SEL 0x12u
#define REG_ALERT_CH_SEL 0x14u
#define REG_ALERT_PIN_CFG 0x17u
#define REG_EVENT_FLAG 0x18u
#define REG_EVENT_HIGH_FLAG 0x1Au
#define REG_EVENT_LOW_FLAG 0x1Cu
#define REG_EVENT_RGN 0x1Eu
#define REG_HYSTERESIS_CH0 0x20u  // four registers per channel: HYSTERESIS, HIGH_TH, EVENT_COUNT, LOW_TH
#define REG_RECENT_CH0_LSB 0xA0u  // two registers per channel

#define GENERAL_RST 0x01u
#define GENERAL_DWC_EN 0x10u    // digital window comparator on
#define GENERAL_STATS_EN 0x20u  // keeps RECENT_CHx updated
#define DATA_APPEND_CHID 0x10u  // channel ID appended to conversion data
#define OPMODE_CONV_AUTO 0x20u
#define OPMODE_OSC_LOW_POWER 0x10u
#define SEQ_START 0x10u
#define SEQ_MODE_AUTO 0x01u
// OSR_CFG = 0 (no averaging), PIN_CFG = 0 (every channel an analog input), ALERT_PIN_CFG = 0 (open drain, active low)

// Region watched by the digital window comparator (EVENT_RGN bit of a channel)
typedef enum {
  ALERT_OUT_OF_BAND = 0,  // flag set below the low or above the high threshold
  ALERT_IN_BAND = 1,      // flag set while high_th < code < low_th (bounds swap registers in-band; verified on the board)
} alert_region_e;

/* Window comparator settings of a single channel. Thresholds are raw 12-bit codes; hysteresis and event_count are the
   raw 4-bit register fields. */
typedef struct {
  bool enabled;         // route this channel to the ALERT pin (ALERT_CH_SEL)
  uint16_t high_th;     // 12-bit high threshold, 0xFFF disables the high side
  uint16_t low_th;      // 12-bit low threshold, 0x000 disables the low side
  uint8_t hysteresis;   // 4-bit, applied as [hysteresis, 000b] on both thresholds
  uint8_t event_count;  // 4-bit, alert after n+1 consecutive violations
  alert_region_e region;
} alert_cfg_t;

typedef struct {
  sys_device_base_t base;  // first: id, suspended (sequencer stopped)
  sys_dev_pin_t intr;  // ALERT: active low, its falling edge runs our ON_EVENT
  sys_i2c_dev_t i2c;
  uint16_t full_scale_mV;  // the input voltage of code 4095

  /* Shadow of the chip configuration, replayed by chip_restore() */
  uint8_t alert_ch_mask;  // ALERT_CH_SEL: channels driving the ALERT pin
  uint8_t event_rgn;      // EVENT_RGN: 1 = in-band alert for that channel
  alert_cfg_t alerts[ADS7128_CHANNELS];
  bool autonomous;                          // sequencer runs on its own (any alert armed)
  uint16_t recent_codes[ADS7128_CHANNELS];  // last 12-bit code read per channel

  uint8_t intr_modes[ADS7128_CHANNELS];  // sys_io_intr_mode_e per channel
  /* The user-facing config from CONFIGURE_INTR, remembered so on_alert can restore it once a crossing has been
     reported and recovery has been detected (the entry / exit watch swap, below). */
  alert_cfg_t entry_alert_cfg[ADS7128_CHANNELS];
  /* Bit N set = channel N is past its threshold and has been reported; the chip is armed with an "exit watch"
     (arm_exit_watch) instead of its entry config. EVENT_HIGH_FLAG / EVENT_LOW_FLAG have no notion of "newly" violating
     vs "still" violating: a signal parked past the threshold re-latches the flag on every autonomous sample. Instead
     of polling for recovery, on_alert flips the armed config between the entry threshold (waiting to cross) and a
     hysteresis-retreated exit threshold (waiting to recover), so the chip itself raises ALERT only on a genuine
     transition in either direction. */
  uint8_t alert_active_mask;
} ads_t;

/* ---- Scaling (integers: 12-bit codes over the configured full scale in mV) ---- */

static uint32_t code_to_mV(const ads_t* c, uint16_t code) {
  return (uint32_t)(((uint64_t)code * c->full_scale_mV + MAX_CODE / 2) / MAX_CODE);
}

static uint16_t mv_to_code(const ads_t* c, uint32_t mV) {
  const uint64_t code = ((uint64_t)mV * MAX_CODE + c->full_scale_mV / 2) / c->full_scale_mV;
  return code > MAX_CODE ? MAX_CODE : (uint16_t)code;
}

static uint8_t hysteresis_field(const ads_t* c, uint32_t hysteresis_mV) {
  const uint32_t steps = mv_to_code(c, hysteresis_mV) / HYSTERESIS_STEP;
  return steps > HYSTERESIS_FIELD_MAX ? HYSTERESIS_FIELD_MAX : (uint8_t)steps;
}

// The field counts n + 1 consecutive violations, so a threshold of 0 or 1 means "alert on the first sample".
static uint8_t event_count_field(uint16_t event_counter_threshold) {
  return event_counter_threshold <= 1 ? 0 : (uint8_t)(event_counter_threshold - 1);
}

static err_h chip_write_reg(ads_t* c, uint8_t reg, uint8_t val) {
  const uint8_t frame[3] = {OP_SINGLE_REGISTER_WRITE, reg, val};
  return sys_i2c_write(&c->i2c, frame, sizeof(frame));
}

static err_h chip_write_block(ads_t* c, uint8_t first_reg, const uint8_t* data, size_t len) {
  uint8_t frame[2 + 4];
  ERR_CHECK(len >= 1 && len <= sizeof(frame) - 2, ERR_I2C_BAD_ARG);
  frame[0] = OP_CONTINUOUS_REGISTER_WRITE;
  frame[1] = first_reg;
  memcpy(&frame[2], data, len);
  return sys_i2c_write(&c->i2c, frame, len + 2);
}

static err_h chip_read_block(ads_t* c, uint8_t first_reg, uint8_t* data, size_t len) {
  const uint8_t frame[2] = {OP_CONTINUOUS_REGISTER_READ, first_reg};
  return sys_i2c_write_read(&c->i2c, frame, sizeof(frame), data, len);
}

static const alert_cfg_t k_alert_off = {
    .enabled = false, .high_th = MAX_CODE, .low_th = 0, .hysteresis = 0, .event_count = 0, .region = ALERT_OUT_OF_BAND};

static void load_defaults(ads_t* c) {
  c->alert_ch_mask = 0;
  c->event_rgn = 0;
  c->autonomous = false;
  for (uint8_t ch = 0; ch < ADS7128_CHANNELS; ch++) {
    c->alerts[ch] = k_alert_off;
  }
  memset(c->recent_codes, 0, sizeof(c->recent_codes));
}

static uint8_t opmode_value(bool autonomous) {
  return (uint8_t)((autonomous ? OPMODE_CONV_AUTO : 0u) | OPMODE_OSC_LOW_POWER | CLK_DIV);
}

// The datasheet requires an idle sequencer before the mode or any channel configuration is changed.
static err_h chip_stop_sequence(ads_t* c) {
  return chip_write_reg(c, REG_SEQUENCE_CFG, 0);
}

static err_h chip_write_alert_block(ads_t* c, uint8_t channel) {
  const alert_cfg_t* alert = &c->alerts[channel];
  /* Thresholds are split across the block: the MSB register holds bits [11:4], the upper nibble of the neighbouring
     register holds bits [3:0]. */
  const uint8_t block[4] = {
      (uint8_t)(((alert->high_th & 0x0F) << 4) | (alert->hysteresis & 0x0F)),  // HYSTERESIS_CHx
      (uint8_t)((alert->high_th >> 4) & 0xFF),                                 // HIGH_TH_CHx
      (uint8_t)(((alert->low_th & 0x0F) << 4) | (alert->event_count & 0x0F)),  // EVENT_COUNT_CHx
      (uint8_t)((alert->low_th >> 4) & 0xFF),                                  // LOW_TH_CHx
  };
  return chip_write_block(c, REG_HYSTERESIS_CH0 + channel * 4u, block, sizeof(block));
}

/* EVENT_FLAG (0x18) is a read-only summary of EVENT_HIGH_FLAG / EVENT_LOW_FLAG (datasheet 8.6.18): clearing only
   ever needs the two writes below. */
static err_h chip_clear_event_flags(ads_t* c, uint8_t high_mask, uint8_t low_mask) {
  if (high_mask) {
    ERR_TRY(chip_write_reg(c, REG_EVENT_HIGH_FLAG, high_mask));
  }
  if (low_mask) {
    ERR_TRY(chip_write_reg(c, REG_EVENT_LOW_FLAG, low_mask));
  }
  return NULL;
}

/* The window comparator only sees codes the ADC actually converts. As long as a channel is armed the chip therefore
   runs the sequencer on its own clock; with no alert armed it falls back to manual, host-triggered conversions. */
static err_h chip_apply_conv_mode(ads_t* c) {
  const bool autonomous = c->alert_ch_mask != 0;
  ERR_TRY(chip_stop_sequence(c));
  if (autonomous) {
    ERR_TRY(chip_write_reg(c, REG_AUTO_SEQ_CH_SEL, CH_MASK_ALL));
    ERR_TRY(chip_write_reg(c, REG_OPMODE_CFG, opmode_value(true)));
    // STATS_EN keeps RECENT_CHx updated, the only way to read a channel while the chip drives the sequence itself.
    ERR_TRY(chip_write_reg(c, REG_GENERAL_CFG, GENERAL_DWC_EN | GENERAL_STATS_EN));
    ERR_TRY(chip_write_reg(c, REG_SEQUENCE_CFG, SEQ_START | SEQ_MODE_AUTO));
  } else {
    ERR_TRY(chip_write_reg(c, REG_GENERAL_CFG, 0));
    ERR_TRY(chip_write_reg(c, REG_OPMODE_CFG, opmode_value(false)));
  }
  c->autonomous = autonomous;
  return NULL;
}

// Replays the shadow configuration (create, reset, resume).
static err_h chip_restore(ads_t* c) {
  ERR_TRY(chip_stop_sequence(c));
  // Appending the channel ID to conversion data lets a manual read prove which channel it actually got.
  ERR_TRY(chip_write_reg(c, REG_DATA_CFG, DATA_APPEND_CHID));
  ERR_TRY(chip_write_reg(c, REG_OSR_CFG, 0));
  ERR_TRY(chip_write_reg(c, REG_PIN_CFG, 0));
  ERR_TRY(chip_write_reg(c, REG_ALERT_PIN_CFG, 0));
  for (uint8_t ch = 0; ch < ADS7128_CHANNELS; ch++) {
    ERR_TRY(chip_write_alert_block(c, ch));
  }
  ERR_TRY(chip_write_reg(c, REG_EVENT_RGN, c->event_rgn));
  ERR_TRY(chip_write_reg(c, REG_ALERT_CH_SEL, c->alert_ch_mask));
  ERR_TRY(chip_clear_event_flags(c, 0xFF, 0xFF));
  return chip_apply_conv_mode(c);
}

static err_h chip_reset(ads_t* c) {
  ERR_TRY(chip_write_reg(c, REG_GENERAL_CFG, GENERAL_RST));
  vTaskDelay(MSEC(RESET_DELAY_MS));
  load_defaults(c);
  return chip_restore(c);
}

static err_h chip_set_alert_cfg(ads_t* c, uint8_t channel, const alert_cfg_t* cfg) {
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
  ERR_TRY(chip_stop_sequence(c));
  ERR_TRY(chip_write_alert_block(c, channel));
  ERR_TRY(chip_write_reg(c, REG_EVENT_RGN, c->event_rgn));
  ERR_TRY(chip_write_reg(c, REG_ALERT_CH_SEL, c->alert_ch_mask));
  // A flag left over from the previous thresholds would hold ALERT asserted.
  ERR_TRY(chip_clear_event_flags(c, ch_bit, ch_bit));
  return chip_apply_conv_mode(c);
}

/* Manual mode: the write frame moves the mux, the read frame that follows starts the conversion and returns
   [D11:D4][D3:D0, CHID]. */
static err_h chip_manual_read(ads_t* c, uint8_t channel, uint16_t* out_code) {
  const uint8_t tx[3] = {OP_SINGLE_REGISTER_WRITE, REG_MANUAL_CH_SEL, (uint8_t)(channel & 0x0F)};
  uint8_t rx[2] = {0};
  for (uint8_t attempt = 0; attempt < MANUAL_READ_TRIES; attempt++) {
    ERR_TRY(sys_i2c_write_read(&c->i2c, tx, sizeof(tx), rx, sizeof(rx)));
    if ((rx[1] & 0x0F) == channel) {
      *out_code = (uint16_t)(((((uint16_t)rx[0]) << 8) | rx[1]) >> 4);
      return NULL;
    }
  }
  return ERR_NEW(ERR_IO_BAD_READING, .id = c->base.id, .pin = channel);
}

/* Autonomous mode: the sequencer owns the mux, so the last conversion result is taken from the statistics block
   instead of from a conversion frame. */
static err_h chip_recent_read(ads_t* c, uint8_t channel, uint16_t* out_code) {
  uint8_t raw[2] = {0};
  ERR_TRY(chip_read_block(c, REG_RECENT_CH0_LSB + channel * 2u, raw, sizeof(raw)));
  // LSB register first, the result is MSB aligned in 16 bits
  *out_code = (uint16_t)(((((uint16_t)raw[1]) << 8) | raw[0]) >> 4);
  return NULL;
}

static err_h chip_read_channel(ads_t* c, uint8_t channel, uint16_t* out_code) {
  uint16_t code = 0;
  ERR_TRY(c->autonomous ? chip_recent_read(c, channel, &code) : chip_manual_read(c, channel, &code));
  c->recent_codes[channel] = code;
  *out_code = code;
  return NULL;
}

// One block read covers EVENT_FLAG (0x18) through EVENT_LOW_FLAG (0x1C).
static err_h chip_get_event_flags(ads_t* c, uint8_t* high, uint8_t* low) {
  uint8_t raw[REG_EVENT_LOW_FLAG - REG_EVENT_FLAG + 1] = {0};
  ERR_TRY(chip_read_block(c, REG_EVENT_FLAG, raw, sizeof(raw)));
  *high = raw[REG_EVENT_HIGH_FLAG - REG_EVENT_FLAG];
  *low = raw[REG_EVENT_LOW_FLAG - REG_EVENT_FLAG];
  return NULL;
}

static err_h check_channel(const ads_t* c, uint8_t pin) {
  ERR_CHECK(pin < ADS7128_CHANNELS, ERR_DEV_BAD_PIN, .id = c->base.id, .pin = pin);
  return NULL;
}

/* ---- Event: the ALERT pin fell ---- */

/* The exit watch is always OUT_OF_BAND. After an out-of-band crossing it is single-sided (one threshold disabled =
   "watch this one side only"); after an in-band entry it watches both sides of the band, since leaving it either way
   is the recovery. */
static void arm_exit_watch(alert_cfg_t* out, const alert_cfg_t* entry, bool high_fired, bool low_fired) {
  *out = (alert_cfg_t){
      .enabled = true,
      .high_th = MAX_CODE,  // disabled unless low_fired narrows it below
      .low_th = 0,          // disabled unless high_fired narrows it above
      .hysteresis = 0,
      .event_count = entry->event_count,
      .region = ALERT_OUT_OF_BAND,
  };
  const uint16_t hyst_codes = (uint16_t)(entry->hysteresis * HYSTERESIS_STEP);
  if (entry->region == ALERT_IN_BAND) {
    // Was inside (entry->high_th, entry->low_th) - the in-band register swap; recovered once it leaves the band on
    // either side, past the hysteresis.
    out->low_th = entry->high_th > hyst_codes ? (uint16_t)(entry->high_th - hyst_codes) : 0;
    const uint32_t above = (uint32_t)entry->low_th + hyst_codes;
    out->high_th = above > MAX_CODE ? MAX_CODE : (uint16_t)above;
    return;
  }
  if (high_fired) {
    // Was above entry->high_th; recovered once it drops back below (high_th - hysteresis).
    out->low_th = entry->high_th > hyst_codes ? (uint16_t)(entry->high_th - hyst_codes) : 0;
  }
  if (low_fired) {
    // Was below entry->low_th; recovered once it rises back above (low_th + hysteresis).
    const uint32_t retreat = (uint32_t)entry->low_th + hyst_codes;
    out->high_th = retreat > MAX_CODE ? MAX_CODE : (uint16_t)retreat;
  }
}

static err_h publish_crossing(ads_t* c, uint8_t pin, uint16_t code) {
  const int32_t mV = (int32_t)code_to_mV(c, code);
  return c->intr_modes[pin] == SYS_IO_INTR_ADC_WINDOW_INSIDE ? SYS_EVENT_PUBLISH(IO, ADC_INSIDE, c->base.id, pin, mV)
                                                              : SYS_EVENT_PUBLISH(IO, ADC_OUTSIDE, c->base.id, pin, mV);
}

/* Publishes each new window crossing. The flags decide which channels fired and are cleared afterwards to release
   ALERT for the next event.

   ALERT is the live OR of the EVENT_FLAG bits (datasheet 8.3.11), and each flag is latched: set on a violation, NOT
   self-clearing when the signal returns in range; only an explicit write-1 clears it. The ESP32 interrupt is
   edge-triggered (falling edge only), so if the sequencer re-latches a flag between our read and our clear (it keeps
   converting autonomously the whole time), a flag can be left set with no further edge ever able to fire and revisit
   it: ALERT would stay asserted forever. Looping until a poll comes back with nothing pending closes that gap. */
EVENT_HANDLER(ON_EVENT) {
  (void)args;
  ads_t* c = ctx;
  if (c->base.suspended) {
    return NULL;
  }
  err_h err = NULL;
  for (int guard = 0; guard < 16; guard++) {
    uint8_t high, low;
    ERR_TRY(chip_get_event_flags(c, &high, &low));
    const uint8_t pending = (uint8_t)(high | low);
    if (!pending) {
      break;
    }
    for (uint8_t pin = 0; pin < ADS7128_CHANNELS; pin++) {
      if (!(pending & (1u << pin)) || c->intr_modes[pin] == SYS_IO_INTR_DISABLE) {
        continue;
      }
      uint16_t code = 0;
      err_h read_err = chip_read_channel(c, pin, &code);
      if (read_err != NULL) {
        err_free(read_err);
        code = c->recent_codes[pin];  // report the last good reading rather than nothing
      }
      if (!(c->alert_active_mask & (1u << pin))) {
        // Entry watch tripped: a genuine new crossing.
        c->alert_active_mask |= (uint8_t)(1u << pin);
        ERR_KEEP_FIRST(err, publish_crossing(c, pin, code));
        alert_cfg_t exit_cfg;
        arm_exit_watch(&exit_cfg, &c->entry_alert_cfg[pin], (high & (1u << pin)) != 0, (low & (1u << pin)) != 0);
        ERR_TRY(chip_set_alert_cfg(c, pin, &exit_cfg));
      } else {
        // Exit watch tripped: genuinely recovered, re-arm the original watch.
        c->alert_active_mask &= (uint8_t)~(1u << pin);
        ERR_TRY(chip_set_alert_cfg(c, pin, &c->entry_alert_cfg[pin]));
      }
    }
    ERR_TRY(chip_clear_event_flags(c, high, low));
  }
  return err;
}

/* ---- IO contract ---- */

// A read: refused while suspended (the sequencer is stopped).
IO_HANDLER(GET_VOLTAGE) {
  ads_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  ERR_TRY(check_channel(c, args->pin));
  uint16_t code = 0;
  ERR_TRY(chip_read_channel(c, args->pin, &code));
  const int32_t mV = (int32_t)code_to_mV(c, code);
  return cmd_reply(out, out_cap, out_len, &mV, sizeof(mV));
}

// Channels are analog inputs out of reset and this device has nothing else, so the only mode that can be honoured is ADC.
IO_HANDLER(SET_MODE) {
  ads_t* c = ctx;
  ERR_TRY(check_channel(c, args->pin));
  ERR_CHECK(args->mode == SYS_IO_MODE_ADC, ERR_IO_MODE_UNSUPPORTED, .id = c->base.id, .pin = args->pin, .mode = args->mode);
  return NULL;
}

/* The on-chip window comparator is the only trigger an analog input has; edge modes belong to digital pins. A
   threshold of 0 mV up means "no high limit": full scale never trips the comparator. */
IO_HANDLER(CONFIGURE_INTR) {
  ads_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  ERR_TRY(check_channel(c, args->pin));
  const sys_io_intr_cfg_t* cfg = &args->cfg;
  ERR_CHECK(cfg->mode <= SYS_IO_INTR_ADC_WINDOW_INSIDE, ERR_CMD_BAD_ARG);

  if (cfg->mode == SYS_IO_INTR_DISABLE) {
    c->intr_modes[args->pin] = SYS_IO_INTR_DISABLE;
    c->alert_active_mask &= (uint8_t)~(1u << args->pin);
    return chip_set_alert_cfg(c, args->pin, &k_alert_off);
  }
  ERR_CHECK(cfg->mode >= SYS_IO_INTR_ADC_WINDOW_OUTSIDE, ERR_IO_FEATURE_UNSUPPORTED, .id = c->base.id, .pin = args->pin);

  const sys_io_adc_window_t* w = &cfg->adc;
  SYS_DEV_CHECK_IN_RANGE(c->base.id, w->threshold_up_mV, 0, c->full_scale_mV);
  ERR_CHECK(w->threshold_down_mV <= c->full_scale_mV && (w->threshold_up_mV == 0 || w->threshold_down_mV <= w->threshold_up_mV),
            ERR_DEV_RANGE, .id = c->base.id, .value = w->threshold_down_mV, .min = 0,
            .max = w->threshold_up_mV ? w->threshold_up_mV : c->full_scale_mV);
  const uint32_t hysteresis_max_mV = code_to_mV(c, (uint16_t)(HYSTERESIS_FIELD_MAX * HYSTERESIS_STEP));
  SYS_DEV_CHECK_IN_RANGE(c->base.id, w->hysteresis_mV, 0, hysteresis_max_mV);
  SYS_DEV_CHECK_IN_RANGE(c->base.id, w->event_counter_threshold, 0, EVENT_COUNT_MAX);

  const uint16_t upper = w->threshold_up_mV == 0 ? MAX_CODE : mv_to_code(c, w->threshold_up_mV);
  const uint16_t lower = mv_to_code(c, w->threshold_down_mV);
  const bool inside = cfg->mode == SYS_IO_INTR_ADC_WINDOW_INSIDE;
  const alert_cfg_t alert = {
      .enabled = true,
      /* In-band (EVENT_RGN = 1) the chip flags HIGH_TH < code < LOW_TH, so the bounds swap registers (verified on the
         board 2026-09-24; datasheet 8.6.21 misprints it as "low threshold > result < high threshold"). */
      .high_th = inside ? lower : upper,
      .low_th = inside ? upper : lower,
      .hysteresis = hysteresis_field(c, w->hysteresis_mV),
      .event_count = event_count_field(w->event_counter_threshold),
      .region = inside ? ALERT_IN_BAND : ALERT_OUT_OF_BAND,
  };
  ERR_TRY(chip_set_alert_cfg(c, args->pin, &alert));
  c->intr_modes[args->pin] = (uint8_t)cfg->mode;
  c->entry_alert_cfg[args->pin] = alert;
  // A fresh config: no crossing reported yet, so the next real violation is dispatched.
  c->alert_active_mask &= (uint8_t)~(1u << args->pin);
  return NULL;
}

// The channel's comparator off.
IO_HANDLER(RESET) {
  ads_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  ERR_TRY(check_channel(c, args->pin));
  c->intr_modes[args->pin] = SYS_IO_INTR_DISABLE;
  c->alert_active_mask &= (uint8_t)~(1u << args->pin);
  return chip_set_alert_cfg(c, args->pin, &k_alert_off);
}

/* ---- Lifecycle ---- */

// Releases the hardware; REMOVE and the create rollback. Each step runs only if it was built, none returns early.
static err_h uninstall(void* ctx) {
  ads_t* c = ctx;
  err_h err = NULL;
  ERR_KEEP_FIRST(err, sys_dev_pin_release(&c->intr));  // ALERT first: no events while the chip stops
  if (sys_i2c_dev_added(&c->i2c)) {
    if (!c->base.suspended) {
      ERR_KEEP_FIRST(err, chip_stop_sequence(c));  // nothing converts after the device is gone
    }
    ERR_KEEP_FIRST(err, sys_i2c_dev_remove(&c->i2c));
  }
  return err;
}

// Back to the chip's defaults: every comparator off.
DEVICE_HANDLER(RESET) {
  ads_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  memset(c->intr_modes, SYS_IO_INTR_DISABLE, sizeof(c->intr_modes));
  c->alert_active_mask = 0;
  return chip_reset(c);
}

/* Safe state: the sequencer stops (nothing converts and no alert is raised); the shadow is kept for RESUME. The
   device counts as suspended even if the write failed. */
static err_h suspend_hook(void* ctx) {
  ads_t* c = ctx;
  return chip_stop_sequence(c);
}

// Replays the configuration. A channel past its threshold when it stopped is armed on its entry config again. A
// failing resume leaves the device suspended.
static err_h resume_hook(void* ctx) {
  ads_t* c = ctx;
  for (uint8_t ch = 0; ch < ADS7128_CHANNELS; ch++) {
    if (c->alert_active_mask & (1u << ch)) {
      c->alerts[ch] = c->entry_alert_cfg[ch];
    }
  }
  c->alert_active_mask = 0;
  ERR_TRY(chip_restore(c));
  return NULL;
}

/* ---- Create ---- */

static err_h create(void* ctx, const void* cfg_in) {
  ads_t* c = ctx;
  const ads7128_cfg_t* cfg = cfg_in;
  const uint16_t id = c->base.id;
  SYS_DEV_CHECK_IN_RANGE(id, cfg->i2c_bus, 0, SYS_I2C_BUS_COUNT - 1);
  SYS_DEV_CHECK_IN_RANGE(id, cfg->i2c_addr, 0x10, 0x17);
  // Every reading and every threshold is scaled by this.
  ERR_CHECK(cfg->full_scale_mV >= 1, ERR_DEV_RANGE, .id = id, .value = cfg->full_scale_mV, .min = 1, .max = UINT16_MAX);
  c->full_scale_mV = cfg->full_scale_mV;
  load_defaults(c);
  memset(c->intr_modes, SYS_IO_INTR_DISABLE, sizeof(c->intr_modes));
  ERR_TRY(sys_i2c_dev_attach(&c->i2c, cfg->i2c_bus, cfg->i2c_addr, I2C_SPEED_HZ));  // probes the chip
  ERR_TRY(chip_reset(c));

  // ALERT: active low, so its falling edge reaches our ON_EVENT (inline: I2C reads, no delay).
  ERR_TRY(sys_dev_pin_take_input(&c->base, &c->intr, cfg->intr_pin, IO_EV_FALLING, SYS_IO_INTR_MODE_FALLING_EDGE));
  return NULL;
}

/* ---- Type ---- */

static const sys_device_op_t k_ops[] = {
    DEVICE_ENTRY(RESET),
    IO_ENTRY(GET_VOLTAGE), IO_ENTRY(SET_MODE),          IO_ENTRY(CONFIGURE_INTR),
    IO_ENTRY(RESET),       EVENT_ENTRY(ON_EVENT),
};

SYS_DEVICE_TYPE_DEFINE(.type = SYS_DEVICE_TYPE_ADS7128, .cfg_size = sizeof(ads7128_cfg_t), .ctx_size = sizeof(ads_t),
                       .create = create, .uninstall = uninstall, .suspend = suspend_hook, .resume = resume_hook,
                       SYS_DEVICE_OPS(k_ops))
