#include "driver_ads7128.h"
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = __FILE_NAME__;

#undef OWNER
#define OWNER OWNER_DEVICE_ADS7128

#define I2C_FREQ_HZ 400000

/* Time the chip needs after a software reset before it answers again */
#define ADS7128_RESET_DELAY_MS 5

/* A conversion frame issued right after a channel change can still carry the
 * previous channel, so a manual read retries until the appended channel ID
 * matches what was asked for. */
#define ADS7128_MANUAL_READ_TRIES 3

/* ~1 kSPS on the low-power oscillator: fast enough for threshold monitoring,
 * slow enough to keep the autonomous sequencer out of the way of I2C traffic */
#define ADS7128_DEFAULT_CLK_DIV 0x0A

#define RETURN_ON_ERROR(x)                   \
  do {                                       \
    esp_err_t __err_rc = (x);                \
    if (__err_rc != ESP_OK) return __err_rc; \
  } while (0)

#define CHECK_DRV_HANDLE(VAL)               \
  do {                                      \
    if (!(VAL)) return ESP_ERR_INVALID_ARG; \
  } while (0)

#define CHECK_DRV_CHANNEL(CH)                                 \
  do {                                                        \
    if ((CH) >= ADS7128_CH_COUNT) return ESP_ERR_INVALID_ARG; \
  } while (0)

#define ADS_TRANSMIT(handle, buf, size) ((handle)->header.transmit ? (handle)->header.transmit((handle), (buf), (size)) : ESP_ERR_INVALID_STATE)

#define ADS_TRANSMIT_RECEIVE(handle, tx_buf, tx_size, rx_buf, rx_size) ((handle)->header.transmit_receive ? (handle)->header.transmit_receive((handle), (tx_buf), (tx_size), (rx_buf), (rx_size)) : ESP_ERR_INVALID_STATE)

/* Every channel owns four consecutive threshold registers, starting at
 * HYSTERESIS_CHx, and two consecutive result registers at RECENT_CHx_LSB. */
#define ADS_ALERT_BLOCK(ch) ((uint8_t)(HYSTERESIS_CH0_ADDRESS + ((ch) * 4)))
#define ADS_RECENT_LSB(ch) ((uint8_t)(RECENT_CH0_LSB_ADDRESS + ((ch) * 2)))

/*******************************************************************************
 * REGISTER ACCESS
 ******************************************************************************/

static esp_err_t _ads_write_reg(ads_handle_t handle, uint8_t reg, uint8_t val) {
  uint8_t buf[3] = {OP_CODE_SINGLE_REGISTER_WRITE, reg, val};
  return ADS_TRANSMIT(handle, buf, sizeof(buf));
}

static esp_err_t _ads_write_block(ads_handle_t handle, uint8_t first_reg, const uint8_t* data, size_t len) {
  uint8_t buf[2 + 4];
  if (len > sizeof(buf) - 2) return ESP_ERR_INVALID_SIZE;

  buf[0] = OP_CODE_CONTINUOUS_REGISTER_WRITE;
  buf[1] = first_reg;
  memcpy(&buf[2], data, len);
  return ADS_TRANSMIT(handle, buf, len + 2);
}

static esp_err_t _ads_read_block(ads_handle_t handle, uint8_t first_reg, uint8_t* data, size_t len) {
  uint8_t buf[2] = {OP_CODE_CONTINUOUS_REGISTER_READ, first_reg};
  return ADS_TRANSMIT_RECEIVE(handle, buf, sizeof(buf), data, len);
}

/*******************************************************************************
 * INTERNAL HELPERS
 ******************************************************************************/

static void _ads_load_defaults(ads_handle_t handle) {
  handle->seq_ch_mask = ADS7128_CH_MASK_ALL;
  handle->alert_ch_mask = 0;
  handle->event_rgn = 0;
  handle->osr = OSR_1;
  handle->clk_div = ADS7128_DEFAULT_CLK_DIV;
  handle->low_power_osc = true;
  handle->alert_push_pull = false;  // open drain, as the chip powers up
  handle->alert_active_high = false;
  handle->conv_mode = ADS7128_CONV_MANUAL;

  for (uint8_t ch = 0; ch < ADS7128_CH_COUNT; ch++) {
    handle->alerts[ch] = (ads7128_alert_cfg_t){
        .enabled = false,
        .high_th = ADS7128_MAX_CODE,
        .low_th = 0,
        .hysteresis = 0,
        .event_count = 0,
        .region = ADS7128_ALERT_OUT_OF_BAND,
    };
  }
  memset(handle->recent_codes, 0, sizeof(handle->recent_codes));
}

static uint8_t _ads_opmode_value(ads_handle_t handle, bool autonomous) {
  uint8_t val = autonomous ? CONV_MODE_AUTO : CONV_MODE_MANUAL;
  val |= handle->low_power_osc ? OSC_SEL_LOW_POWER : OSC_SEL_HIGH_SPEED;
  val |= (uint8_t)(handle->clk_div & 0x0F);
  return val;
}

static uint8_t _ads_alert_pin_value(ads_handle_t handle) {
  uint8_t val = handle->alert_push_pull ? ALERT_DRIVE_PUSH_PULL : ALERT_DRIVE_OPEN_DRAIN;
  val |= handle->alert_active_high ? ALERT_LOGIC_ACTIVE_HIGH : ALERT_LOGIC_ACTIVE_LOW;
  return val;
}

/* Stop the sequencer. The datasheet requires an idle sequencer before the mode
 * or any channel configuration is changed. */
static esp_err_t _ads_stop_sequence(ads_handle_t handle) {
  return _ads_write_reg(handle, SEQUENCE_CFG_ADDRESS, SEQ_START_END | SEQ_MODE_MANUAL);
}

static esp_err_t _ads_write_alert_block(ads_handle_t handle, uint8_t channel) {
  const ads7128_alert_cfg_t* alert = &handle->alerts[channel];

  /* Thresholds are split across the block: the MSB register holds bits [11:4],
   * the upper nibble of the neighbouring register holds bits [3:0]. */
  uint8_t block[4] = {
      (uint8_t)(((alert->high_th & 0x0F) << 4) | (alert->hysteresis & 0x0F)),  // HYSTERESIS_CHx
      (uint8_t)((alert->high_th >> 4) & 0xFF),                                 // HIGH_TH_CHx
      (uint8_t)(((alert->low_th & 0x0F) << 4) | (alert->event_count & 0x0F)),  // EVENT_COUNT_CHx
      (uint8_t)((alert->low_th >> 4) & 0xFF),                                  // LOW_TH_CHx
  };
  return _ads_write_block(handle, ADS_ALERT_BLOCK(channel), block, sizeof(block));
}

/* The window comparator only sees codes the ADC actually converts. As long as a
 * channel is armed the chip therefore runs the sequencer on its own clock; with
 * no alert armed it falls back to manual, host-triggered conversions. */
static esp_err_t _ads_apply_conv_mode(ads_handle_t handle) {
  bool autonomous = (handle->alert_ch_mask != 0);

  RETURN_ON_ERROR(_ads_stop_sequence(handle));

  if (autonomous) {
    RETURN_ON_ERROR(_ads_write_reg(handle, AUTO_SEQ_CH_SEL_ADDRESS, handle->seq_ch_mask));
    RETURN_ON_ERROR(_ads_write_reg(handle, OPMODE_CFG_ADDRESS, _ads_opmode_value(handle, true)));
    /* STATS_EN keeps RECENT_CHx updated, which is the only way to read a channel
     * while the device drives the sequence itself. */
    RETURN_ON_ERROR(_ads_write_reg(handle, GENERAL_CFG_ADDRESS, DWC_EN_ENABLE | STATS_EN_ENABLE_CLEAR));
    RETURN_ON_ERROR(_ads_write_reg(handle, SEQUENCE_CFG_ADDRESS, SEQ_START_ASSEND | SEQ_MODE_AUTO));
  } else {
    RETURN_ON_ERROR(_ads_write_reg(handle, GENERAL_CFG_ADDRESS, DWC_EN_DISABLE_RESET | STATS_EN_DISABLE));
    RETURN_ON_ERROR(_ads_write_reg(handle, OPMODE_CFG_ADDRESS, _ads_opmode_value(handle, false)));
  }

  handle->conv_mode = autonomous ? ADS7128_CONV_AUTONOMOUS : ADS7128_CONV_MANUAL;
  return ESP_OK;
}

/* Manual mode: the write frame moves the mux, the read frame that follows starts
 * the conversion and returns [D11:D4][D3:D0, CHID]. */
static esp_err_t _ads_manual_read(ads_handle_t handle, uint8_t channel, uint16_t* out_code) {
  uint8_t tx[3] = {OP_CODE_SINGLE_REGISTER_WRITE, MANUAL_CH_SEL_ADDRESS, (uint8_t)(channel & MANUAL_CHID_MASK)};
  uint8_t rx[3] = {0};

  /* Without averaging the frame is [D11:D4][D3:D0, CHID]; with averaging the
   * result grows to a full 16 bits and the channel ID moves to a third byte.
   * Either way the first two bytes are the MSB-aligned result. */
  bool averaged = (handle->osr != OSR_1);
  size_t rx_len = averaged ? 3 : 2;
  uint8_t chid = 0;

  for (uint8_t attempt = 0; attempt < ADS7128_MANUAL_READ_TRIES; attempt++) {
    RETURN_ON_ERROR(ADS_TRANSMIT_RECEIVE(handle, tx, sizeof(tx), rx, rx_len));
    chid = averaged ? (uint8_t)(rx[2] >> 4) : (uint8_t)(rx[1] & 0x0F);
    if (chid == channel) {
      *out_code = (uint16_t)(((((uint16_t)rx[0]) << 8) | rx[1]) >> 4);
      return ESP_OK;
    }
  }

  ESP_LOGW(TAG, "manual read of channel %u kept returning channel %u", (unsigned)channel, (unsigned)chid);
  return ESP_ERR_INVALID_RESPONSE;
}

/* Autonomous mode: the sequencer owns the mux, so the last conversion result is
 * taken from the statistics block instead of from a conversion frame. */
static esp_err_t _ads_recent_read(ads_handle_t handle, uint8_t channel, uint16_t* out_code) {
  uint8_t raw[2] = {0};
  RETURN_ON_ERROR(_ads_read_block(handle, ADS_RECENT_LSB(channel), raw, sizeof(raw)));

  /* LSB register first, result is MSB aligned in 16 bits */
  *out_code = (uint16_t)(((((uint16_t)raw[1]) << 8) | raw[0]) >> 4);
  return ESP_OK;
}

/*******************************************************************************
 * API FUNCTIONS
 ******************************************************************************/

esp_err_t ads_restore_state(ads_handle_t handle) {
  CHECK_DRV_HANDLE(handle);

  RETURN_ON_ERROR(_ads_stop_sequence(handle));

  /* Appending the channel ID to conversion data lets a manual read prove which
   * channel it actually got. */
  RETURN_ON_ERROR(_ads_write_reg(handle, DATA_CFG_ADDRESS, APPEND_STATUS_ID));
  RETURN_ON_ERROR(_ads_write_reg(handle, OSR_CFG_ADDRESS, (uint8_t)(handle->osr & OSR_MASK)));
  RETURN_ON_ERROR(_ads_write_reg(handle, PIN_CFG_ADDRESS, PIN_CFG_DEFAULT));  // every channel an analog input
  RETURN_ON_ERROR(_ads_write_reg(handle, ALERT_PIN_CFG_ADDRESS, _ads_alert_pin_value(handle)));

  for (uint8_t ch = 0; ch < ADS7128_CH_COUNT; ch++) {
    RETURN_ON_ERROR(_ads_write_alert_block(handle, ch));
  }

  RETURN_ON_ERROR(_ads_write_reg(handle, EVENT_RGN_ADDRESS, handle->event_rgn));
  RETURN_ON_ERROR(_ads_write_reg(handle, ALERT_CH_SEL_ADDRESS, handle->alert_ch_mask));
  RETURN_ON_ERROR(ads_clear_event_flags(handle, EVENT_FLAG_MASK, EVENT_FLAG_MASK));

  return _ads_apply_conv_mode(handle);
}

esp_err_t ads_reset(ads_handle_t handle) {
  CHECK_DRV_HANDLE(handle);

  RETURN_ON_ERROR(_ads_write_reg(handle, GENERAL_CFG_ADDRESS, RST_START));
  vTaskDelay(pdMS_TO_TICKS(ADS7128_RESET_DELAY_MS));

  _ads_load_defaults(handle);
  return ads_restore_state(handle);
}

esp_err_t ads_start(ads_handle_t handle) {
  CHECK_DRV_HANDLE(handle);
  RETURN_ON_ERROR(ads_reset(handle));
  ESP_LOGI(TAG, "ADS7128 at 0x%02X started", handle->header.i2c_device_config.device_address);
  return ESP_OK;
}

esp_err_t ads_set_sampling(ads_handle_t handle, uint8_t osr, bool low_power_osc, uint8_t clk_div) {
  CHECK_DRV_HANDLE(handle);
  if (osr > OSR_128 || clk_div > 0x0F) return ESP_ERR_INVALID_ARG;

  handle->osr = osr;
  handle->low_power_osc = low_power_osc;
  handle->clk_div = clk_div;

  RETURN_ON_ERROR(_ads_write_reg(handle, OSR_CFG_ADDRESS, (uint8_t)(handle->osr & OSR_MASK)));
  return _ads_apply_conv_mode(handle);
}

esp_err_t ads_set_alert_pin_cfg(ads_handle_t handle, bool push_pull, bool active_high) {
  CHECK_DRV_HANDLE(handle);

  handle->alert_push_pull = push_pull;
  handle->alert_active_high = active_high;
  return _ads_write_reg(handle, ALERT_PIN_CFG_ADDRESS, _ads_alert_pin_value(handle));
}

esp_err_t ads_set_alert_cfg(ads_handle_t handle, uint8_t channel, const ads7128_alert_cfg_t* cfg) {
  CHECK_DRV_HANDLE(handle);
  CHECK_DRV_HANDLE(cfg);
  CHECK_DRV_CHANNEL(channel);
  if (cfg->high_th > ADS7128_MAX_CODE || cfg->low_th > ADS7128_MAX_CODE) return ESP_ERR_INVALID_ARG;
  if (cfg->hysteresis > 0x0F || cfg->event_count > 0x0F) return ESP_ERR_INVALID_ARG;

  handle->alerts[channel] = *cfg;

  uint8_t ch_bit = (uint8_t)(1u << channel);
  if (cfg->enabled) {
    handle->alert_ch_mask |= ch_bit;
  } else {
    handle->alert_ch_mask &= (uint8_t)~ch_bit;
  }
  if (cfg->region == ADS7128_ALERT_IN_BAND) {
    handle->event_rgn |= ch_bit;
  } else {
    handle->event_rgn &= (uint8_t)~ch_bit;
  }

  RETURN_ON_ERROR(_ads_stop_sequence(handle));
  RETURN_ON_ERROR(_ads_write_alert_block(handle, channel));
  RETURN_ON_ERROR(_ads_write_reg(handle, EVENT_RGN_ADDRESS, handle->event_rgn));
  RETURN_ON_ERROR(_ads_write_reg(handle, ALERT_CH_SEL_ADDRESS, handle->alert_ch_mask));
  /* A flag left over from the previous thresholds would hold ALERT asserted */
  RETURN_ON_ERROR(ads_clear_event_flags(handle, ch_bit, ch_bit));

  return _ads_apply_conv_mode(handle);
}

esp_err_t ads_clear_alert_cfg(ads_handle_t handle, uint8_t channel) {
  CHECK_DRV_HANDLE(handle);
  CHECK_DRV_CHANNEL(channel);

  ads7128_alert_cfg_t cleared = {
      .enabled = false,
      .high_th = ADS7128_MAX_CODE,
      .low_th = 0,
      .hysteresis = 0,
      .event_count = 0,
      .region = ADS7128_ALERT_OUT_OF_BAND,
  };
  return ads_set_alert_cfg(handle, channel, &cleared);
}

esp_err_t ads_read_channel(ads_handle_t handle, uint8_t channel, uint16_t* out_code) {
  CHECK_DRV_HANDLE(handle);
  CHECK_DRV_CHANNEL(channel);

  uint16_t code = 0;
  if (handle->conv_mode == ADS7128_CONV_AUTONOMOUS) {
    RETURN_ON_ERROR(_ads_recent_read(handle, channel, &code));
  } else {
    RETURN_ON_ERROR(_ads_manual_read(handle, channel, &code));
  }

  handle->recent_codes[channel] = code;
  if (out_code) *out_code = code;
  return ESP_OK;
}

esp_err_t ads_read_channels(ads_handle_t handle, uint8_t ch_mask) {
  CHECK_DRV_HANDLE(handle);

  for (uint8_t ch = 0; ch < ADS7128_CH_COUNT; ch++) {
    if (!(ch_mask & (1u << ch))) continue;
    RETURN_ON_ERROR(ads_read_channel(handle, ch, NULL));
  }
  return ESP_OK;
}

esp_err_t ads_get_event_flags(ads_handle_t handle, ads7128_event_flags_t* out_flags) {
  CHECK_DRV_HANDLE(handle);
  CHECK_DRV_HANDLE(out_flags);

  /* One block read covers EVENT_FLAG (0x18) through EVENT_LOW_FLAG (0x1C) */
  uint8_t raw[EVENT_LOW_FLAG_ADDRESS - EVENT_FLAG_ADDRESS + 1] = {0};
  RETURN_ON_ERROR(_ads_read_block(handle, EVENT_FLAG_ADDRESS, raw, sizeof(raw)));

  out_flags->any = raw[0];
  out_flags->high = raw[EVENT_HIGH_FLAG_ADDRESS - EVENT_FLAG_ADDRESS];
  out_flags->low = raw[EVENT_LOW_FLAG_ADDRESS - EVENT_FLAG_ADDRESS];
  return ESP_OK;
}

esp_err_t ads_clear_event_flags(ads_handle_t handle, uint8_t high_mask, uint8_t low_mask) {
  CHECK_DRV_HANDLE(handle);

  if (high_mask) RETURN_ON_ERROR(_ads_write_reg(handle, EVENT_HIGH_FLAG_ADDRESS, high_mask));
  if (low_mask) RETURN_ON_ERROR(_ads_write_reg(handle, EVENT_LOW_FLAG_ADDRESS, low_mask));
  return ESP_OK;
}

/*******************************************************************************
 * TWO-PHASE INITIALIZATION
 ******************************************************************************/

ads_handle_t ads_new(uint8_t i2c_address, bool i2c_bus_num) {
  ads_handle_t handle = calloc(1, sizeof(ads_data_t));
  if (!handle) {
    ESP_LOGE(TAG, "Failed to allocate memory for ADS7128 handle");
    return NULL;
  }

  handle->header.i2c_device_config.device_address = i2c_address;
  handle->header.i2c_device_config.scl_speed_hz = I2C_FREQ_HZ;
  handle->header.i2c_device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  handle->header.bus_num = i2c_bus_num;

  _ads_load_defaults(handle);

  return handle;
}

void ads_delete(ads_handle_t handle) {
  if (!handle) return;
  free(handle);
}
