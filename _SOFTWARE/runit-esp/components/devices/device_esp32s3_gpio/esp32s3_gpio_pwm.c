#include "esp32s3_gpio_pwm.h"
#include "driver/ledc.h"
#include "sdkconfig.h"

#define PWM_MODE LEDC_LOW_SPEED_MODE

#define TIMER_MASK   CONFIG_DEVICE_ESP32S3_GPIO_PWM_TIMER_MASK
#define CHANNEL_MASK CONFIG_DEVICE_ESP32S3_GPIO_PWM_CHANNEL_MASK
#define TIMERS       ((uint8_t)__builtin_popcount(TIMER_MASK))    // for the error payloads
#define CHANNELS     ((uint8_t)__builtin_popcount(CHANNEL_MASK))

_Static_assert((TIMER_MASK >> SOC_LEDC_TIMER_NUM) == 0, "PWM timer mask names a timer this chip doesn't have");
_Static_assert((CHANNEL_MASK >> SOC_LEDC_CHANNEL_NUM) == 0, "PWM channel mask names a channel this chip doesn't have");

static struct {
  struct {
    uint32_t frequency_Hz;
    uint8_t bits;  /* duty resolution */
    uint8_t users; /* 0 = free (not configured) */
  } timer[SOC_LEDC_TIMER_NUM];
  uint32_t channels_used; /* bit per LEDC channel */
} s_pwm;

/* Finest resolution the clock gives at this frequency (divider >= 1). */
static uint8_t resolution_bits(uint32_t frequency_Hz) {
  uint8_t bits = 0;
  while (bits < SOC_LEDC_TIMER_BIT_WIDTH && ((uint64_t)frequency_Hz << (bits + 1)) <= ESP32S3_GPIO_PWM_SRC_CLK_HZ) {
    bits++;
  }
  return bits;
}

/* Contract duty -> timer counts; IO_PWM_DUTY_MAX gives 2^bits (always on). */
static uint32_t hw_duty(uint16_t duty, uint8_t bits) {
  return (uint32_t)((((uint64_t)duty << bits) + IO_PWM_DUTY_MAX / 2) / IO_PWM_DUTY_MAX);
}

static uint8_t find_timer(bool in_use, uint32_t frequency_Hz) {
  for (uint8_t t = 0; t < SOC_LEDC_TIMER_NUM; t++) {
    if (!(TIMER_MASK & (1u << t))) continue;  // kept for another LEDC user
    const bool match = in_use ? s_pwm.timer[t].users && s_pwm.timer[t].frequency_Hz == frequency_Hz
                              : !s_pwm.timer[t].users;
    if (match) return t;
  }
  return ESP32S3_GPIO_PWM_TIMER_NONE;
}

/* (Re)configure timer t at frequency_Hz; its users are left to the caller. */
static err_h timer_start(uint8_t t, uint32_t frequency_Hz) {
  const uint8_t bits = resolution_bits(frequency_Hz);
  const ledc_timer_config_t cfg = {
      .speed_mode = PWM_MODE,
      .duty_resolution = (ledc_timer_bit_t)bits,
      .timer_num = (ledc_timer_t)t,
      .freq_hz = frequency_Hz,
      .clk_cfg = LEDC_USE_APB_CLK,
  };
  ERR_TRY_ESP(ledc_timer_config(&cfg));
  s_pwm.timer[t].frequency_Hz = frequency_Hz;
  s_pwm.timer[t].bits = bits;
  return NULL;
}

/* Release timer t once nothing uses it (the driver requires a pause first). */
static err_h timer_stop(uint8_t t) {
  err_h err = ERR_ESP(ledc_timer_pause(PWM_MODE, (ledc_timer_t)t));
  const ledc_timer_config_t cfg = {.speed_mode = PWM_MODE, .timer_num = (ledc_timer_t)t, .deconfigure = true};
  ERR_KEEP_FIRST(err, ERR_ESP(ledc_timer_config(&cfg)));
  s_pwm.timer[t].frequency_Hz = 0;
  s_pwm.timer[t].users = 0;
  return err;
}

static err_h timer_drop(uint8_t t) {
  return --s_pwm.timer[t].users ? NULL : timer_stop(t);
}

static err_h channel_write_duty(const pin_pwm_data_t* p, uint8_t t) {
  ERR_TRY_ESP(ledc_set_duty(PWM_MODE, (ledc_channel_t)p->channel, hw_duty(p->duty, s_pwm.timer[t].bits)));
  return ERR_ESP(ledc_update_duty(PWM_MODE, (ledc_channel_t)p->channel));
}

/* Route the pin's channel to timer t at the pin's duty. The first bind configures
   the channel (and takes the pin); a move only switches the timer, since
   configuring again would re-reserve a pin LEDC already holds. */
static err_h channel_bind(const esp32s3_gpio_pin_t* pin, uint8_t t) {
  const pin_pwm_data_t* p = &pin->hw.pwm;
  if (p->timer != ESP32S3_GPIO_PWM_TIMER_NONE) {
    ERR_TRY_ESP(ledc_bind_channel_timer(PWM_MODE, (ledc_channel_t)p->channel, (ledc_timer_t)t));
    return channel_write_duty(p, t);
  }
  const ledc_channel_config_t cfg = {
      .gpio_num = pin->io_num,
      .speed_mode = PWM_MODE,
      .channel = (ledc_channel_t)p->channel,
      .timer_sel = (ledc_timer_t)t,
      .duty = hw_duty(p->duty, s_pwm.timer[t].bits),
      .hpoint = 0,
  };
  return ERR_ESP(ledc_channel_config(&cfg));
}

err_h esp32s3_gpio_pwm_claim(esp32s3_gpio_pin_t* pin) {
  for (uint8_t c = 0; c < SOC_LEDC_CHANNEL_NUM; c++) {
    if (!(CHANNEL_MASK & (1u << c)) || (s_pwm.channels_used & (1u << c))) continue;
    s_pwm.channels_used |= 1u << c;
    pin->hw.pwm = (pin_pwm_data_t){.channel = c, .timer = ESP32S3_GPIO_PWM_TIMER_NONE};
    return NULL;
  }
  return ERR_NEW(ERR_IO_PWM_CHANNELS_EXHAUSTED, .id = esp32s3_gpio_device_id(), .pin = pin->io_num,
                 .channels = CHANNELS);
}

err_h esp32s3_gpio_pwm_set_frequency(esp32s3_gpio_pin_t* pin, uint32_t frequency_Hz) {
  pin_pwm_data_t* p = &pin->hw.pwm;
  const uint8_t own = p->timer;
  if (own != ESP32S3_GPIO_PWM_TIMER_NONE && s_pwm.timer[own].frequency_Hz == frequency_Hz) return NULL;

  uint8_t target = find_timer(true, frequency_Hz);
  if (target == ESP32S3_GPIO_PWM_TIMER_NONE && own != ESP32S3_GPIO_PWM_TIMER_NONE && s_pwm.timer[own].users == 1) {
    // No other pin on this timer: retune it in place, then rescale the duty to the new resolution.
    ERR_TRY(timer_start(own, frequency_Hz));
    p->frequency_Hz = frequency_Hz;
    return channel_write_duty(p, own);
  }

  const bool fresh = target == ESP32S3_GPIO_PWM_TIMER_NONE;
  if (fresh) {
    target = find_timer(false, 0);
    ERR_CHECK(target != ESP32S3_GPIO_PWM_TIMER_NONE, ERR_IO_PWM_TIMERS_EXHAUSTED, .id = esp32s3_gpio_device_id(),
              .pin = pin->io_num, .timers = TIMERS, .frequency_Hz = frequency_Hz);
    ERR_TRY(timer_start(target, frequency_Hz));
  }
  err_h err = channel_bind(pin, target);
  if (err != NULL) {
    if (fresh) {
      ERR_KEEP_FIRST(err, timer_stop(target));  // the bind error is the one to report
    }
    return err;
  }
  s_pwm.timer[target].users++;
  p->timer = target;
  p->frequency_Hz = frequency_Hz;
  return own != ESP32S3_GPIO_PWM_TIMER_NONE ? timer_drop(own) : NULL;
}

err_h esp32s3_gpio_pwm_set_duty(esp32s3_gpio_pin_t* pin, uint16_t duty) {
  pin_pwm_data_t* p = &pin->hw.pwm;
  const uint16_t previous = p->duty;
  p->duty = duty;
  if (p->timer != ESP32S3_GPIO_PWM_TIMER_NONE) return channel_write_duty(p, p->timer);
  // First write without a frequency: bind at the default one (the bind applies the duty).
  const err_h err = esp32s3_gpio_pwm_set_frequency(pin, CONFIG_DEVICE_ESP32S3_GPIO_PWM_DEFAULT_FREQ_HZ);
  if (err != NULL) p->duty = previous;
  return err;
}

err_h esp32s3_gpio_pwm_release(esp32s3_gpio_pin_t* pin) {
  pin_pwm_data_t* p = &pin->hw.pwm;
  err_h err = NULL;
  if (p->timer != ESP32S3_GPIO_PWM_TIMER_NONE) {
    ERR_KEEP_FIRST(err, ERR_ESP(ledc_stop(PWM_MODE, (ledc_channel_t)p->channel, 0)));
    const ledc_channel_config_t cfg = {
        .speed_mode = PWM_MODE, .channel = (ledc_channel_t)p->channel, .deconfigure = true};
    ERR_KEEP_FIRST(err, ERR_ESP(ledc_channel_config(&cfg)));
    ERR_KEEP_FIRST(err, timer_drop(p->timer));
    p->timer = ESP32S3_GPIO_PWM_TIMER_NONE;
  }
  s_pwm.channels_used &= ~(1u << p->channel);
  return err;
}
