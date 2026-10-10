#include "esp_pwm.h"
#include "driver/ledc.h"
#include "sdkconfig.h"

#define PWM_MODE LEDC_LOW_SPEED_MODE

_Static_assert((CONFIG_DEVICE_GPIO_ESP_PWM_TIMER_MASK >> SOC_LEDC_TIMER_NUM) == 0, "PWM timer mask names a timer this chip doesn't have");
_Static_assert((CONFIG_DEVICE_GPIO_ESP_PWM_CHANNEL_MASK >> SOC_LEDC_CHANNEL_NUM) == 0, "PWM channel mask names a channel this chip doesn't have");

static struct {
  uint32_t frequency_Hz;
  uint8_t bits;  /* duty resolution */
  uint8_t users; /* 0 = free (not configured) */
} s_timer[SOC_LEDC_TIMER_NUM];

static uint32_t s_channels_used; /* bit per LEDC channel */

/* Finest resolution the clock gives at this frequency (divider >= 1). */
static uint8_t resolution_bits(uint32_t frequency_Hz) {
  uint8_t bits = 0;
  while (bits < SOC_LEDC_TIMER_BIT_WIDTH && ((uint64_t)frequency_Hz << (bits + 1)) <= ESP_PWM_SRC_CLK_HZ) bits++;
  return bits;
}

/* Device duty -> timer counts; ESP_PWM_DUTY_FULL gives 2^bits (always on). */
static uint32_t hw_duty(uint16_t duty, uint8_t bits) {
  return (uint32_t)((((uint64_t)duty << bits) + ESP_PWM_DUTY_FULL / 2) / ESP_PWM_DUTY_FULL);
}

static uint8_t find_timer(bool in_use, uint32_t frequency_Hz) {
  for (uint8_t t = 0; t < SOC_LEDC_TIMER_NUM; t++) {
    if (!(CONFIG_DEVICE_GPIO_ESP_PWM_TIMER_MASK & (1u << t))) continue;  // kept for another LEDC user
    if (in_use ? (s_timer[t].users && s_timer[t].frequency_Hz == frequency_Hz) : !s_timer[t].users) return t;
  }
  return ESP_PWM_TIMER_NONE;
}

/* (Re)configure timer t at frequency_Hz; its users are left to the caller. */
static esp_err_t timer_start(uint8_t t, uint32_t frequency_Hz) {
  uint8_t bits = resolution_bits(frequency_Hz);
  ledc_timer_config_t cfg = {
      .speed_mode = PWM_MODE,
      .duty_resolution = (ledc_timer_bit_t)bits,
      .timer_num = (ledc_timer_t)t,
      .freq_hz = frequency_Hz,
      .clk_cfg = LEDC_USE_APB_CLK,
  };
  esp_err_t err = ledc_timer_config(&cfg);
  if (err != ESP_OK) return err;
  s_timer[t].frequency_Hz = frequency_Hz;
  s_timer[t].bits = bits;
  return ESP_OK;
}

/* Release timer t once nothing uses it (the driver requires a pause first). */
static esp_err_t timer_stop(uint8_t t) {
  esp_err_t err = ledc_timer_pause(PWM_MODE, (ledc_timer_t)t);
  ledc_timer_config_t cfg = {.speed_mode = PWM_MODE, .timer_num = (ledc_timer_t)t, .deconfigure = true};
  esp_err_t del = ledc_timer_config(&cfg);
  s_timer[t].frequency_Hz = 0;
  s_timer[t].users = 0;
  return err != ESP_OK ? err : del;
}

static esp_err_t timer_drop(uint8_t t) {
  if (--s_timer[t].users) return ESP_OK;
  return timer_stop(t);
}

/* Route the pin's channel to timer t at the pin's duty. The first bind configures
   the channel (and takes the pin); a move only switches the timer, since
   configuring again would re-reserve a pin LEDC already holds. */
static esp_err_t channel_bind(const esp_pin_obj_t* pin, uint8_t t) {
  const pin_pwm_data_t* p = &pin->hw.pwm_cfg;
  if (p->timer != ESP_PWM_TIMER_NONE) {
    esp_err_t err = ledc_bind_channel_timer(PWM_MODE, (ledc_channel_t)p->channel, (ledc_timer_t)t);
    if (err == ESP_OK) err = ledc_set_duty(PWM_MODE, (ledc_channel_t)p->channel, hw_duty(p->duty, s_timer[t].bits));
    return err != ESP_OK ? err : ledc_update_duty(PWM_MODE, (ledc_channel_t)p->channel);
  }
  ledc_channel_config_t cfg = {
      .gpio_num = pin->io_num,
      .speed_mode = PWM_MODE,
      .channel = (ledc_channel_t)p->channel,
      .timer_sel = (ledc_timer_t)t,
      .duty = hw_duty(p->duty, s_timer[t].bits),
      .hpoint = 0,
  };
  return ledc_channel_config(&cfg);
}

static esp_err_t channel_write_duty(const esp_pin_obj_t* pin) {
  const pin_pwm_data_t* p = &pin->hw.pwm_cfg;
  esp_err_t err = ledc_set_duty(PWM_MODE, (ledc_channel_t)p->channel, hw_duty(p->duty, s_timer[p->timer].bits));
  return err != ESP_OK ? err : ledc_update_duty(PWM_MODE, (ledc_channel_t)p->channel);
}

esp_err_t esp_pwm_claim(esp_pin_obj_t* pin) {
  for (uint8_t c = 0; c < SOC_LEDC_CHANNEL_NUM; c++) {
    if (!(CONFIG_DEVICE_GPIO_ESP_PWM_CHANNEL_MASK & (1u << c)) || (s_channels_used & (1u << c))) continue;
    s_channels_used |= 1u << c;
    pin->hw.pwm_cfg = (pin_pwm_data_t){.channel = c, .timer = ESP_PWM_TIMER_NONE};
    return ESP_OK;
  }
  return ESP_ERR_NOT_FOUND;
}

esp_err_t esp_pwm_set_frequency(esp_pin_obj_t* pin, uint32_t frequency_Hz) {
  pin_pwm_data_t* p = &pin->hw.pwm_cfg;
  uint8_t own = p->timer;
  if (own != ESP_PWM_TIMER_NONE && s_timer[own].frequency_Hz == frequency_Hz) return ESP_OK;

  uint8_t target = find_timer(true, frequency_Hz);
  if (target == ESP_PWM_TIMER_NONE && own != ESP_PWM_TIMER_NONE && s_timer[own].users == 1) {
    // No other pin on this timer: retune it in place, then rescale the duty to the new resolution.
    esp_err_t err = timer_start(own, frequency_Hz);
    if (err != ESP_OK) return err;
    p->frequency_Hz = frequency_Hz;
    return channel_write_duty(pin);
  }

  bool fresh = target == ESP_PWM_TIMER_NONE;
  if (fresh) {
    target = find_timer(false, 0);
    if (target == ESP_PWM_TIMER_NONE) return ESP_ERR_NOT_FOUND;
    esp_err_t err = timer_start(target, frequency_Hz);
    if (err != ESP_OK) return err;
  }
  esp_err_t err = channel_bind(pin, target);
  if (err != ESP_OK) {
    if (fresh) (void)timer_stop(target);  // the bind error is the one to report
    return err;
  }
  s_timer[target].users++;
  p->timer = target;
  p->frequency_Hz = frequency_Hz;
  return own != ESP_PWM_TIMER_NONE ? timer_drop(own) : ESP_OK;
}

esp_err_t esp_pwm_set_duty(esp_pin_obj_t* pin, uint32_t duty) {
  pin_pwm_data_t* p = &pin->hw.pwm_cfg;
  uint16_t previous = p->duty;
  p->duty = (uint16_t)duty;
  if (p->timer != ESP_PWM_TIMER_NONE) return channel_write_duty(pin);
  // First write without a frequency: bind at the default one (the bind applies the duty).
  esp_err_t err = esp_pwm_set_frequency(pin, CONFIG_DEVICE_GPIO_ESP_PWM_DEFAULT_FREQ_HZ);
  if (err != ESP_OK) p->duty = previous;
  return err;
}

esp_err_t esp_pwm_release(esp_pin_obj_t* pin) {
  pin_pwm_data_t* p = &pin->hw.pwm_cfg;
  esp_err_t first = ESP_OK;
  if (p->timer != ESP_PWM_TIMER_NONE) {
    esp_err_t err = ledc_stop(PWM_MODE, (ledc_channel_t)p->channel, 0);
    if (first == ESP_OK) first = err;
    ledc_channel_config_t cfg = {.speed_mode = PWM_MODE, .channel = (ledc_channel_t)p->channel, .deconfigure = true};
    err = ledc_channel_config(&cfg);
    if (first == ESP_OK) first = err;
    err = timer_drop(p->timer);
    if (first == ESP_OK) first = err;
    p->timer = ESP_PWM_TIMER_NONE;
  }
  s_channels_used &= ~(1u << p->channel);
  return first;
}
