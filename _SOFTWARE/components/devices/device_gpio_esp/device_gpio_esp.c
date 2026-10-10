#include "device_gpio_esp.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc_config.h"
#include "esp_pwm.h"
#include "esp_timer.h"
#include "gpio_esp_pin.h"
#include "soc/soc_caps.h"
#include "sys_event.h"
#include "sys_pins.h"
#include "utils.h"

#define DEBOUNCE_US ((int64_t)CONFIG_DEVICE_GPIO_ESP_DEBOUNCE_MS * 1000)

esp_pin_obj_t pin_pool[GPIO_NUM_MAX];
uint64_t configured_pins;
uint16_t gpio_esp_device_id;

/* Guards pin_pool / configured_pins and the PWM pools against the ADC task. Every
 * call into the device is already serialized by sys_device; this one is for the task. */
U_MUTEX_DEFINE(gpio_mutex);

#define LOCK() ((void)U_MUTEX_LOCK(gpio_mutex, WAIT_FOREVER))
#define UNLOCK() ((void)U_MUTEX_UNLOCK(gpio_mutex))

typedef struct {
  sys_device_base_t base;  // first: id, suspended
  sys_pins_state_t pins;
} gpio_t;

static gpio_t* s_gpio;  // the ESP's pins are a singleton: the pin pool is the real state

// Mode -> ESP-IDF gpio_config_t fields. GPIO_MODE_DISABLE (0) marks a mode this
// table doesn't support, so validity is a single lookup.
static const struct {
  gpio_mode_t mode;
  gpio_pullup_t pull_up;
  gpio_pulldown_t pull_down;
} k_gpio_mode_map[] = {
    [SYS_IO_MODE_INPUT] = {GPIO_MODE_INPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE},
    [SYS_IO_MODE_INPUT_PULLUP] = {GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE},
    [SYS_IO_MODE_INPUT_PULLDOWN] = {GPIO_MODE_INPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_ENABLE},
    [SYS_IO_MODE_OUTPUT_PUSH_PULL] = {GPIO_MODE_INPUT_OUTPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE},
    [SYS_IO_MODE_OUTPUT_OPEN_DRAIN] = {GPIO_MODE_INPUT_OUTPUT_OD, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE},
    [SYS_IO_MODE_OUTPUT_OPEN_DRAIN_PULLUP] = {GPIO_MODE_INPUT_OUTPUT_OD, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE},
};

// Interrupt mode -> ESP-IDF interrupt type of a digital pin. GPIO_INTR_DISABLE (0) marks a mode a digital pin
// can't do (the ADC window modes).
static const gpio_int_type_t k_gpio_intr_map[] = {
    [SYS_IO_INTR_MODE_RISING_EDGE] = GPIO_INTR_POSEDGE,
    [SYS_IO_INTR_MODE_FALLING_EDGE] = GPIO_INTR_NEGEDGE,
    [SYS_IO_INTR_MODE_BOTH_EDGES] = GPIO_INTR_ANYEDGE,
};

// Inputs and outputs driven by the GPIO matrix: the pins whose edges the ISR can see.
static bool is_digital(sys_io_mode_e mode) {
  return mode <= SYS_IO_MODE_OUTPUT_OPEN_DRAIN_PULLUP;
}

static bool is_digital_output(sys_io_mode_e mode) {
  return mode == SYS_IO_MODE_OUTPUT_PUSH_PULL || mode == SYS_IO_MODE_OUTPUT_OPEN_DRAIN ||
         mode == SYS_IO_MODE_OUTPUT_OPEN_DRAIN_PULLUP;
}

// A write to an unlocked pin: not suspended, the pin exists and nobody has locked it.
static err_h check_writable(const gpio_t* c, uint8_t pin) {
  ERR_TRY(sys_device_check_active(&c->base));
  return sys_pins_state_check_free(&c->pins, c->base.id, pin);
}

// The pin's object (gpio_mutex held), or ERR_IO_PIN_UNCONFIGURED.
static err_h pin_configured(const gpio_t* c, uint8_t pin, esp_pin_obj_t** obj) {
  *obj = pin_obj_get(pin);
  ERR_CHECK(*obj != NULL, ERR_IO_PIN_UNCONFIGURED, .id = c->base.id, .pin = pin);
  return NULL;
}

static uint64_t configured_mask(void) {
  LOCK();
  const uint64_t mask = configured_pins;
  UNLOCK();
  return mask;
}

/* ---- Pin setup and release ---- */

// The pin back to unconfigured. Every step runs; the first failure is returned.
static err_h release_pin(uint8_t pin) {
  bool was_adc = false;
  bool was_configured = false;
  adc_cali_handle_t cali_handle = NULL;
  esp_err_t pwm_err = ESP_OK;
  esp_err_t isr_err = ESP_OK;

  LOCK();
  esp_pin_obj_t* pin_obj = pin_obj_get(pin);
  if (pin_obj != NULL) {
    was_configured = true;
    was_adc = (pin_obj->pin_mode == SYS_IO_MODE_ADC);
    cali_handle = pin_obj->hw.adc_cfg.cali_handle;
    // Under the lock: the channel and timer pools are shared by every PWM pin.
    if (pin_obj->pin_mode == SYS_IO_MODE_PWM) {
      pwm_err = esp_pwm_release(pin_obj);
    }
    // Before the slot is cleared: the ISR reads it.
    if (is_digital(pin_obj->pin_mode) && pin_obj->intr.mode != SYS_IO_INTR_DISABLE) {
      isr_err = gpio_isr_handler_remove((gpio_num_t)pin);
    }
    configured_pins &= ~(1ULL << pin);
    memset(pin_obj, 0, sizeof(*pin_obj));
  }
  UNLOCK();

  if (!was_configured) {
    return NULL;
  }

  err_h err = NULL;
  SYS_DEVICE_KEEP_FIRST(err, ERR_ESP(pwm_err));
  SYS_DEVICE_KEEP_FIRST(err, ERR_ESP(isr_err));
  SYS_DEVICE_KEEP_FIRST(err, ERR_ESP(gpio_reset_pin((gpio_num_t)pin)));
  if (was_adc) {
    if (cali_handle != NULL) {
      SYS_DEVICE_KEEP_FIRST(err, ERR_ESP(adc_cali_delete_scheme_curve_fitting(cali_handle)));
    }
    SYS_DEVICE_KEEP_FIRST(err, ERR_ESP(esp_adc_update_active_channels()));
  }
  return err;
}

// Puts an unconfigured pin into `mode` (gpio_mutex held). *adc_changed: the ADC channel set must be refreshed
// afterwards, outside the lock (it takes gpio_mutex again).
static err_h configure_pin(const gpio_t* c, uint8_t pin, uint8_t mode, bool* adc_changed) {
  esp_pin_obj_t* existing = pin_obj_get(pin);
  ERR_CHECK(existing == NULL, ERR_IO_PIN_IN_USE, .id = c->base.id, .pin = pin, .mode = existing->pin_mode);

  esp_pin_obj_t* new_pin = &pin_pool[pin];
  memset(new_pin, 0, sizeof(*new_pin));
  new_pin->io_num = pin;
  new_pin->pin_mode = (sys_io_mode_e)mode;

  if (mode == SYS_IO_MODE_ADC) {
    adc_channel_t channel = 0;
    adc_unit_t unit = 0;
    ERR_CHECK(adc_continuous_io_to_channel(pin, &unit, &channel) == ESP_OK && unit == ADC_UNIT_1,
              ERR_IO_FEATURE_UNSUPPORTED, .id = c->base.id, .pin = pin);
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = SOC_ADC_DIGI_MAX_BITWIDTH,
    };
    ERR_TRY_ESP(adc_cali_create_scheme_curve_fitting(&cali_config, &new_pin->hw.adc_cfg.cali_handle));
    configured_pins |= (1ULL << pin);
    *adc_changed = true;
    return NULL;
  }

  if (mode == SYS_IO_MODE_PWM) {
    ERR_CHECK((SOC_GPIO_VALID_OUTPUT_GPIO_MASK & (1ULL << pin)) != 0, ERR_IO_MODE_UNSUPPORTED, .id = c->base.id,
              .pin = pin, .mode = mode);
    ERR_CHECK(esp_pwm_claim(new_pin) == ESP_OK, ERR_IO_PWM_CHANNELS_EXHAUSTED, .id = c->base.id, .pin = pin,
              .channels = ESP_PWM_CHANNELS);
    // Held low until the first frequency or duty write binds a timer. Not gpio_config():
    // it reserves an output pin, and LEDC then warns the pin is taken.
    esp_err_t rc = gpio_set_level((gpio_num_t)pin, 0);
    if (rc == ESP_OK) rc = gpio_set_pull_mode((gpio_num_t)pin, GPIO_FLOATING);
    if (rc == ESP_OK) rc = gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    if (rc != ESP_OK) {
      (void)esp_pwm_release(new_pin);  // only frees the channel: no timer is bound yet
      return ERR_ESP(rc);
    }
    configured_pins |= (1ULL << pin);
    return NULL;
  }

  ERR_CHECK(mode < ARRAY_LEN(k_gpio_mode_map) && k_gpio_mode_map[mode].mode != GPIO_MODE_DISABLE,
            ERR_IO_MODE_UNSUPPORTED, .id = c->base.id, .pin = pin, .mode = mode);
  gpio_config_t cfg = {
      .pin_bit_mask = (1ULL << pin),
      .mode = k_gpio_mode_map[mode].mode,
      .pull_up_en = k_gpio_mode_map[mode].pull_up,
      .pull_down_en = k_gpio_mode_map[mode].pull_down,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ERR_TRY_ESP(gpio_config(&cfg));
  new_pin->hw.gpio_cfg = cfg;
  configured_pins |= (1ULL << pin);
  return NULL;
}

/* ---- IO contract ---- */

IO_HANDLER(SET_MODE) {
  gpio_t* c = ctx;
  ERR_TRY(check_writable(c, a->pin));
  bool adc_changed = false;
  LOCK();
  err_h err = configure_pin(c, a->pin, a->mode, &adc_changed);
  UNLOCK();
  if (err == NULL && adc_changed) {
    err = ERR_ESP(esp_adc_update_active_channels());
  }
  return err;
}

IO_HANDLER(RESET) {
  gpio_t* c = ctx;
  ERR_TRY(check_writable(c, a->pin));
  return release_pin(a->pin);
}

// Drives a push-pull / open-drain pin. The caller has checked who may write it.
static err_h write_level(const gpio_t* c, uint8_t pin, bool level) {
  esp_pin_obj_t* pin_obj;
  LOCK();
  err_h err = pin_configured(c, pin, &pin_obj);
  if (err == NULL && !is_digital_output(pin_obj->pin_mode)) {
    err = ERR_NEW(ERR_IO_WRONG_MODE, .id = c->base.id, .pin = pin, .mode = pin_obj->pin_mode);
  }
  if (err == NULL) {
    err = ERR_ESP(gpio_set_level((gpio_num_t)pin, level));
  }
  UNLOCK();
  return err;
}

IO_HANDLER(SET_LEVEL) {
  gpio_t* c = ctx;
  ERR_CHECK(a->level <= 1, ERR_CMD_BAD_ARG);
  ERR_TRY(check_writable(c, a->pin));
  return write_level(c, a->pin, a->level != 0);
}

IO_HANDLER(TOGGLE) {
  gpio_t* c = ctx;
  ERR_TRY(check_writable(c, a->pin));
  esp_pin_obj_t* pin_obj;
  LOCK();
  err_h err = pin_configured(c, a->pin, &pin_obj);
  if (err == NULL && !is_digital_output(pin_obj->pin_mode)) {
    err = ERR_NEW(ERR_IO_WRONG_MODE, .id = c->base.id, .pin = a->pin, .mode = pin_obj->pin_mode);
  }
  if (err == NULL) {
    err = ERR_ESP(gpio_set_level((gpio_num_t)a->pin, gpio_get_level((gpio_num_t)a->pin) == 0));
  }
  UNLOCK();
  return err;
}

// A read: allowed while suspended and on a locked pin.
IO_HANDLER(GET_LEVEL) {
  const gpio_t* c = ctx;
  ERR_TRY(sys_pins_state_check(&c->pins, c->base.id, a->pin));
  esp_pin_obj_t* pin_obj;
  LOCK();
  err_h err = pin_configured(c, a->pin, &pin_obj);
  if (err == NULL && (pin_obj->pin_mode == SYS_IO_MODE_ADC || pin_obj->pin_mode == SYS_IO_MODE_PWM)) {
    err = ERR_NEW(ERR_IO_WRONG_MODE, .id = c->base.id, .pin = a->pin, .mode = pin_obj->pin_mode);
  }
  const uint8_t level = err == NULL ? (gpio_get_level((gpio_num_t)a->pin) != 0) : 0;
  UNLOCK();
  ERR_TRY(err);
  return cmd_reply(out, out_cap, out_len, &level, sizeof(level));
}

// esp_adc_get_mV() checks that the pin is configured in ADC mode.
IO_HANDLER(GET_VOLTAGE) {
  const gpio_t* c = ctx;
  ERR_TRY(sys_pins_state_check(&c->pins, c->base.id, a->pin));
  int32_t mV = 0;
  ERR_CHECK(esp_adc_get_mV(a->pin, &mV) == ESP_OK, ERR_IO_FEATURE_UNSUPPORTED, .id = c->base.id, .pin = a->pin);
  return cmd_reply(out, out_cap, out_len, &mV, sizeof(mV));
}

/* ---- Pin interrupts ---- */

/* Edge of an armed digital pin. The ISR service is installed without ESP_INTR_FLAG_IRAM, so this runs with the
 * cache on and may call flash code; sys_event_publish defers the whole delivery to the event task. */
static void pin_isr(void* arg) {
  esp_pin_obj_t* pin = arg;
  const uint8_t mode = pin->intr.mode;
  if (mode == SYS_IO_INTR_DISABLE) {
    return;
  }
  // Only switches are debounced. A chip alert line (debounce off) holds a
  // latched flag: an edge dropped here would never be followed by another,
  // so the flag would stay set and the line stuck asserted.
  if (pin->intr.debounce) {
    const int64_t now_us = esp_timer_get_time();
    if (now_us - pin->last_isr_us < DEBOUNCE_US) {
      return;
    }
    pin->last_isr_us = now_us;
  }
  // A one-edge mode publishes its own edge (the level may have bounced back already); both edges: the level now.
  const bool rising = mode == SYS_IO_INTR_MODE_RISING_EDGE ||
                      (mode == SYS_IO_INTR_MODE_BOTH_EDGES && gpio_get_level((gpio_num_t)pin->io_num) != 0);
  const sys_event_t ev = {.source = gpio_esp_device_id,
                          .event = rising ? IO_EV_RISING : IO_EV_FALLING,
                          .channel = pin->io_num,
                          .flags = rising ? IO_EV_RISING_FLAGS : IO_EV_FALLING_FLAGS,
                          .value = rising};
  err_h err = sys_event_publish(&ev);  // from an ISR always NULL: a drop is counted and reported by sys_event
  (void)err;
}

// Arms / disarms a digital pin's edge interrupt (gpio_mutex held).
static err_h arm_digital(const gpio_t* c, esp_pin_obj_t* pin_obj, const sys_io_intr_cfg_t* cfg) {
  const gpio_num_t pin = (gpio_num_t)pin_obj->io_num;
  if (cfg->mode == SYS_IO_INTR_DISABLE) {
    pin_obj->intr = *cfg;
    err_h err = ERR_ESP(gpio_isr_handler_remove(pin));
    SYS_DEVICE_KEEP_FIRST(err, ERR_ESP(gpio_set_intr_type(pin, GPIO_INTR_DISABLE)));
    return err;
  }
  ERR_CHECK(cfg->mode < ARRAY_LEN(k_gpio_intr_map) && k_gpio_intr_map[cfg->mode] != GPIO_INTR_DISABLE,
            ERR_IO_FEATURE_UNSUPPORTED, .id = c->base.id, .pin = pin_obj->io_num);
  pin_obj->intr = *cfg;
  pin_obj->last_isr_us = 0;
  err_h err = ERR_ESP(gpio_set_intr_type(pin, k_gpio_intr_map[cfg->mode]));
  if (err == NULL) {
    err = ERR_ESP(gpio_isr_handler_add(pin, pin_isr, pin_obj));  // enables the interrupt; replaces an earlier handler
  }
  if (err != NULL) {
    pin_obj->intr.mode = SYS_IO_INTR_DISABLE;
    (void)gpio_isr_handler_remove(pin);  // only clears the slot; the cause above is what gets reported
  }
  return err;
}

// Arms / disarms an ADC pin's thresholds (gpio_mutex held); the ADC task checks them on every reading.
static err_h arm_adc(const gpio_t* c, esp_pin_obj_t* pin_obj, const sys_io_intr_cfg_t* cfg) {
  ERR_CHECK(cfg->mode != SYS_IO_INTR_MODE_BOTH_EDGES, ERR_IO_FEATURE_UNSUPPORTED, .id = c->base.id, .pin = pin_obj->io_num);
  if (cfg->mode == SYS_IO_INTR_ADC_WINDOW_OUTSIDE || cfg->mode == SYS_IO_INTR_ADC_WINDOW_INSIDE) {
    ERR_CHECK(cfg->adc.threshold_down_mV <= cfg->adc.threshold_up_mV, ERR_DEV_RANGE, .id = c->base.id,
              .value = cfg->adc.threshold_down_mV, .min = 0, .max = cfg->adc.threshold_up_mV);
  }
  pin_obj->intr = *cfg;
  pin_obj->hw.adc_cfg.alert_active = false;
  pin_obj->hw.adc_cfg.past_count = 0;
  return NULL;
}

/* Digital pin (input or output): RISING / FALLING / BOTH edges from the ISR, value = level. ADC pin: RISING
 * (>= up), FALLING (<= down), ADC_OUTSIDE, ADC_INSIDE from the ADC task, value = mV. Events: SYS_IO_EVENTS,
 * channel = pin. Re-arming replaces the old config. */
IO_HANDLER(CONFIGURE_INTR) {
  gpio_t* c = ctx;
  ERR_CHECK(a->cfg.mode <= SYS_IO_INTR_ADC_WINDOW_INSIDE && a->cfg.debounce <= 1, ERR_CMD_BAD_ARG);
  ERR_TRY(check_writable(c, a->pin));
  esp_pin_obj_t* pin_obj;
  LOCK();
  err_h err = pin_configured(c, a->pin, &pin_obj);
  if (err == NULL) {
    if (pin_obj->pin_mode == SYS_IO_MODE_ADC) {
      err = arm_adc(c, pin_obj, &a->cfg);
    } else if (is_digital(pin_obj->pin_mode)) {
      err = arm_digital(c, pin_obj, &a->cfg);
    } else {
      err = ERR_NEW(ERR_IO_WRONG_MODE, .id = c->base.id, .pin = a->pin, .mode = pin_obj->pin_mode);
    }
  }
  UNLOCK();
  return err;
}

// The pin's object if it is in PWM mode (gpio_mutex held), else sets *err.
static esp_pin_obj_t* pwm_pin(const gpio_t* c, uint8_t pin, err_h* err) {
  esp_pin_obj_t* pin_obj;
  *err = pin_configured(c, pin, &pin_obj);
  if (*err == NULL && pin_obj->pin_mode != SYS_IO_MODE_PWM) {
    *err = ERR_NEW(ERR_IO_WRONG_MODE, .id = c->base.id, .pin = pin, .mode = pin_obj->pin_mode);
  }
  return *err == NULL ? pin_obj : NULL;
}

// A pool refusal (no free timer) is reported as such; anything else is a driver failure.
static err_h pwm_result(const gpio_t* c, esp_err_t rc, uint8_t pin, uint32_t frequency_Hz) {
  if (rc == ESP_ERR_NOT_FOUND) {
    return ERR_NEW(ERR_IO_PWM_TIMERS_EXHAUSTED, .id = c->base.id, .pin = pin, .timers = ESP_PWM_TIMERS,
                   .frequency_Hz = frequency_Hz);
  }
  return ERR_ESP(rc);
}

IO_HANDLER(SET_PWM_FREQ) {
  gpio_t* c = ctx;
  ERR_TRY(check_writable(c, a->pin));
  ERR_CHECK(a->frequency_Hz >= ESP_PWM_FREQ_MIN_HZ && a->frequency_Hz <= ESP_PWM_FREQ_MAX_HZ, ERR_DEV_RANGE,
            .id = c->base.id, .value = a->frequency_Hz, .min = ESP_PWM_FREQ_MIN_HZ, .max = ESP_PWM_FREQ_MAX_HZ);
  err_h err;
  LOCK();
  esp_pin_obj_t* pin_obj = pwm_pin(c, a->pin, &err);
  if (pin_obj != NULL) {
    err = pwm_result(c, esp_pwm_set_frequency(pin_obj, a->frequency_Hz), a->pin, a->frequency_Hz);
  }
  UNLOCK();
  return err;
}

IO_HANDLER(SET_PWM_DUTY) {
  gpio_t* c = ctx;
  ERR_TRY(check_writable(c, a->pin));
  ERR_CHECK(a->duty <= IO_PWM_DUTY_MAX, ERR_DEV_RANGE, .id = c->base.id, .value = a->duty, .min = 0,
            .max = IO_PWM_DUTY_MAX);
  err_h err;
  LOCK();
  esp_pin_obj_t* pin_obj = pwm_pin(c, a->pin, &err);
  if (pin_obj != NULL) {
    err = pwm_result(c, esp_pwm_set_duty(pin_obj, a->duty), a->pin, CONFIG_DEVICE_GPIO_ESP_PWM_DEFAULT_FREQ_HZ);
  }
  UNLOCK();
  return err;
}

/* ---- Pins contract ---- */

PINS_HANDLER(AVAILABLE) {
  const gpio_t* c = ctx;
  return sys_pins_reply(out, out_cap, out_len, c->pins.available);
}

PINS_HANDLER(LOCKED) {
  const gpio_t* c = ctx;
  return sys_pins_reply(out, out_cap, out_len, c->pins.locked);
}

PINS_HANDLER(LOCK) {
  gpio_t* c = ctx;
  return sys_pins_state_lock(&c->pins, c->base.id, a->pin);
}

PINS_HANDLER(UNLOCK) {
  gpio_t* c = ctx;
  return sys_pins_state_unlock(&c->pins, c->base.id, a->pin);
}

// The owner of a locked pin (PCA9685 OE, a chip RST) drives it here. SUSPEND leaves locked pins to their owner,
// so this works while the device is suspended; on an unlocked pin it is a plain write.
PINS_HANDLER(SET_LOCKED_LEVEL) {
  gpio_t* c = ctx;
  ERR_TRY(sys_pins_state_check(&c->pins, c->base.id, a->pin));
  ERR_CHECK(a->level <= 1, ERR_CMD_BAD_ARG);
  if ((c->pins.locked & (1ULL << a->pin)) == 0) {
    ERR_TRY(check_writable(c, a->pin));
  }
  return write_level(c, a->pin, a->level != 0);
}

/* ---- Lifecycle ---- */

// Releases every pin (locked ones too when `all`). Every pin is tried.
static err_h release_pins(const gpio_t* c, bool all) {
  err_h err = NULL;
  const uint64_t mask = configured_mask();
  for (uint8_t pin = 0; pin < GPIO_NUM_MAX; pin++) {
    if ((mask & (1ULL << pin)) && (all || (c->pins.locked & (1ULL << pin)) == 0)) {
      SYS_DEVICE_KEEP_FIRST(err, release_pin(pin));
    }
  }
  return err;
}

// A pin locked by another device (TCA6424A's RST) is left to its owner: the sweep goes low -> high ID, so the
// owner's own reset still needs the pin configured.
DEVICE_HANDLER(RESET) {
  return release_pins(ctx, false);
}

/* Fault safe state of the pins that belong to the user: PWM at duty 0, push-pull outputs low, open-drain outputs
 * released. Inputs and ADC pins have nothing to make safe. A pin locked by another device is left to that device
 * (it makes it safe in its own SUSPEND). The state is kept for RESUME; writes are refused (ERR_DEV_SUSPENDED)
 * while suspended, so it can't go stale. Every pin is reached even if one fails. */
DEVICE_HANDLER(SUSPEND) {
  gpio_t* c = ctx;
  err_h err = NULL;
  c->base.suspended = true;
  LOCK();
  for (uint8_t pin = 0; pin < GPIO_NUM_MAX; pin++) {
    esp_pin_obj_t* pin_obj = pin_obj_get(pin);
    if (pin_obj == NULL || pin_obj->suspended || (c->pins.locked & (1ULL << pin))) {
      continue;
    }
    if (pin_obj->pin_mode == SYS_IO_MODE_PWM) {
      if (pin_obj->hw.pwm_cfg.duty == 0) {
        continue;  // already off (and possibly no timer bound yet)
      }
      pin_obj->hw.pwm_cfg.saved_duty = pin_obj->hw.pwm_cfg.duty;
      pin_obj->suspended = true;
      SYS_DEVICE_KEEP_FIRST(err, ERR_ESP(esp_pwm_set_duty(pin_obj, 0)));
    } else if (is_digital_output(pin_obj->pin_mode)) {
      pin_obj->saved_level = gpio_get_level((gpio_num_t)pin) != 0;
      pin_obj->suspended = true;
      // Open-drain: released (high); push-pull: low.
      SYS_DEVICE_KEEP_FIRST(err, ERR_ESP(gpio_set_level((gpio_num_t)pin, pin_obj->pin_mode != SYS_IO_MODE_OUTPUT_PUSH_PULL)));
    }
  }
  UNLOCK();
  return err;
}

// Brings back what SUSPEND saved: PWM duties and output levels. A pin that fails stays marked, and the device
// stays suspended, so RESUME can be repeated.
DEVICE_HANDLER(RESUME) {
  gpio_t* c = ctx;
  err_h err = NULL;
  LOCK();
  for (uint8_t pin = 0; pin < GPIO_NUM_MAX; pin++) {
    esp_pin_obj_t* pin_obj = pin_obj_get(pin);
    if (pin_obj == NULL || !pin_obj->suspended) {
      continue;
    }
    err_h step = pin_obj->pin_mode == SYS_IO_MODE_PWM ? ERR_ESP(esp_pwm_set_duty(pin_obj, pin_obj->hw.pwm_cfg.saved_duty))
                                                      : ERR_ESP(gpio_set_level((gpio_num_t)pin, pin_obj->saved_level));
    if (step == NULL) {
      pin_obj->suspended = false;
    }
    SYS_DEVICE_KEEP_FIRST(err, step);
  }
  UNLOCK();
  if (err == NULL) {
    c->base.suspended = false;
  }
  return err;
}

/* ---- Create ---- */

// Every pin released (locked ones too): REMOVE and the create rollback.
static err_h uninstall(void* ctx) {
  gpio_t* c = ctx;
  err_h err = release_pins(c, true);
  if (s_gpio == c) {
    s_gpio = NULL;
  }
  return err;
}

// The registry allocated `ctx` (id set); on a failure it calls uninstall() and frees it. No create struct.
static err_h create(void* ctx, const void* cfg) {
  (void)cfg;
  gpio_t* c = ctx;
  ERR_CHECK(s_gpio == NULL, ERR_DEV_ID_TAKEN, .id = s_gpio->base.id);
  sys_pins_state_init(&c->pins, 0);
  c->pins.available = SOC_GPIO_VALID_GPIO_MASK;  // not contiguous: GPIO 22-25 don't exist on the S3

  // Once per boot: REMOVE leaves the service installed (a second install answers ESP_ERR_INVALID_STATE).
  const esp_err_t rc = gpio_install_isr_service(0);
  ERR_TRY(ERR_ESP(rc == ESP_ERR_INVALID_STATE ? ESP_OK : rc));
  ERR_TRY(ERR_ESP(esp_adc_start()));
  gpio_esp_device_id = c->base.id;
  s_gpio = c;
  return NULL;
}

/* ---- Type ---- */

// No SET_VOLTAGE (a DAC does that). OPS lists this table.
static const sys_device_op_t k_ops[] = {
    DEVICE_ENTRY(RESET),     DEVICE_ENTRY(SUSPEND),     DEVICE_ENTRY(RESUME),
    IO_ENTRY(SET_LEVEL),     IO_ENTRY(TOGGLE),          IO_ENTRY(GET_LEVEL),        IO_ENTRY(SET_PWM_DUTY),
    IO_ENTRY(SET_PWM_FREQ),  IO_ENTRY(SET_MODE),        IO_ENTRY(RESET),            IO_ENTRY(GET_VOLTAGE),
    IO_ENTRY(CONFIGURE_INTR), PINS_ENTRY(AVAILABLE),    PINS_ENTRY(LOCKED),         PINS_ENTRY(LOCK),
    PINS_ENTRY(UNLOCK),      PINS_ENTRY(SET_LOCKED_LEVEL),
};

SYS_DEVICE_TYPE_DEFINE(.type = SYS_DEVICE_TYPE_GPIO_ESP, .cfg_size = sizeof(gpio_esp_cfg_t), .ctx_size = sizeof(gpio_t),
                       .create = create, .uninstall = uninstall, SYS_DEVICE_OPS(k_ops))
