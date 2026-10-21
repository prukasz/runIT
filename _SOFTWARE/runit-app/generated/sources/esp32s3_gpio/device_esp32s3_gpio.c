#include "device_esp32s3_gpio.h"

#include "driver/gpio.h"
#include "esp32s3_gpio_adc.h"
#include "esp32s3_gpio_pin.h"
#include "esp32s3_gpio_pwm.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_timer.h"
#include "soc/soc_caps.h"
#include "sys_event.h"
#include "sys_pins.h"
#include "utils.h"

#define DEBOUNCE_US ((int64_t)CONFIG_DEVICE_ESP32S3_GPIO_DEBOUNCE_MS * 1000)

typedef struct {
  sys_device_base_t base;  // first: id, suspended
  sys_pins_state_t pins;
} gpio_t;

/* The chip's pins are one device, and its real state is this table: shared with the ISR and the ADC task, so it is
 * not in the device's struct. */
static struct {
  esp32s3_gpio_pin_t pin[GPIO_NUM_MAX];
  uint64_t configured;  // bit per pin: its slot is live
  gpio_t* dev;          // the instance (a second CREATE is refused)
  uint16_t device_id;   // the source of the pin events
} s_gpio;

/* Guards s_gpio against the ADC task. Every call into the device is already serialized by sys_device. */
U_MUTEX_DEFINE(gpio_mutex);

esp32s3_gpio_pin_t* esp32s3_gpio_pin(uint8_t pin) {
  return pin < GPIO_NUM_MAX && (s_gpio.configured & (1ULL << pin)) ? &s_gpio.pin[pin] : NULL;
}

void esp32s3_gpio_lock(void) {
  (void)U_MUTEX_LOCK(gpio_mutex, WAIT_FOREVER);
}

bool esp32s3_gpio_try_lock(TickType_t ticks) {
  return U_MUTEX_LOCK(gpio_mutex, ticks) == pdTRUE;
}

void esp32s3_gpio_unlock(void) {
  (void)U_MUTEX_UNLOCK(gpio_mutex);
}

uint16_t esp32s3_gpio_device_id(void) {
  return s_gpio.device_id;
}

#define LOCK()   esp32s3_gpio_lock()
#define UNLOCK() esp32s3_gpio_unlock()

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

static bool is_pwm(sys_io_mode_e mode) {
  return mode == SYS_IO_MODE_PWM;
}

static bool is_adc(sys_io_mode_e mode) {
  return mode == SYS_IO_MODE_ADC;
}

// The pin's state (lock held), or ERR_IO_PIN_UNCONFIGURED.
static err_h pin_configured(const gpio_t* c, uint8_t pin, esp32s3_gpio_pin_t** p) {
  *p = esp32s3_gpio_pin(pin);
  ERR_CHECK(*p != NULL, ERR_IO_PIN_UNCONFIGURED, .id = c->base.id, .pin = pin);
  return NULL;
}

// Like pin_configured, and the pin's mode must be one `mode_ok` accepts: else ERR_IO_WRONG_MODE (lock held).
static err_h pin_in_mode(const gpio_t* c, uint8_t pin, bool (*mode_ok)(sys_io_mode_e), esp32s3_gpio_pin_t** p) {
  ERR_TRY(pin_configured(c, pin, p));
  ERR_CHECK(mode_ok((*p)->pin_mode), ERR_IO_WRONG_MODE, .id = c->base.id, .pin = pin, .mode = (*p)->pin_mode);
  return NULL;
}

/* ---- Pin setup and release ---- */

// ADC1 at 12 dB, curve-fitting calibration; the ADC task samples it once the channels are updated.
static err_h configure_adc(const gpio_t* c, esp32s3_gpio_pin_t* p) {
  adc_unit_t unit = 0;
  ERR_CHECK(adc_continuous_io_to_channel(p->io_num, &unit, &p->hw.adc.channel) == ESP_OK && unit == ADC_UNIT_1,
            ERR_IO_FEATURE_UNSUPPORTED, .id = c->base.id, .pin = p->io_num);
  const adc_cali_curve_fitting_config_t cali = {
      .unit_id = ADC_UNIT_1,
      .chan = p->hw.adc.channel,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = SOC_ADC_DIGI_MAX_BITWIDTH,
  };
  return ERR_ESP(adc_cali_create_scheme_curve_fitting(&cali, &p->hw.adc.cali_handle));
}

// A channel reserved, the pin held low until the first frequency or duty write binds a timer. Not gpio_config(): it
// reserves an output pin, and LEDC then warns the pin is taken.
static err_h configure_pwm(const gpio_t* c, esp32s3_gpio_pin_t* p) {
  const gpio_num_t pin = (gpio_num_t)p->io_num;
  ERR_CHECK((SOC_GPIO_VALID_OUTPUT_GPIO_MASK & (1ULL << pin)) != 0, ERR_IO_MODE_UNSUPPORTED, .id = c->base.id,
            .pin = p->io_num, .mode = SYS_IO_MODE_PWM);
  ERR_TRY(esp32s3_gpio_pwm_claim(p));
  err_h err = ERR_ESP(gpio_set_level(pin, 0));
  if (err == NULL) err = ERR_ESP(gpio_set_pull_mode(pin, GPIO_FLOATING));
  if (err == NULL) err = ERR_ESP(gpio_set_direction(pin, GPIO_MODE_OUTPUT));
  if (err != NULL) {
    ERR_KEEP_FIRST(err, esp32s3_gpio_pwm_release(p));  // only frees the channel: no timer is bound yet
  }
  return err;
}

static err_h configure_digital(const gpio_t* c, const esp32s3_gpio_pin_t* p) {
  const uint8_t mode = p->pin_mode;
  ERR_CHECK(mode < ARRAY_LEN(k_gpio_mode_map) && k_gpio_mode_map[mode].mode != GPIO_MODE_DISABLE,
            ERR_IO_MODE_UNSUPPORTED, .id = c->base.id, .pin = p->io_num, .mode = mode);
  const gpio_config_t cfg = {
      .pin_bit_mask = (1ULL << p->io_num),
      .mode = k_gpio_mode_map[mode].mode,
      .pull_up_en = k_gpio_mode_map[mode].pull_up,
      .pull_down_en = k_gpio_mode_map[mode].pull_down,
      .intr_type = GPIO_INTR_DISABLE,
  };
  return ERR_ESP(gpio_config(&cfg));
}

// Puts an unconfigured pin into `mode` (lock held); its slot goes live only when that worked.
static err_h configure_pin(const gpio_t* c, uint8_t pin, uint8_t mode) {
  const esp32s3_gpio_pin_t* existing = esp32s3_gpio_pin(pin);
  ERR_CHECK(existing == NULL, ERR_IO_PIN_IN_USE, .id = c->base.id, .pin = pin, .mode = existing->pin_mode);
  esp32s3_gpio_pin_t* p = &s_gpio.pin[pin];
  *p = (esp32s3_gpio_pin_t){.io_num = pin, .pin_mode = (sys_io_mode_e)mode};
  ERR_TRY(mode == SYS_IO_MODE_ADC   ? configure_adc(c, p)
          : mode == SYS_IO_MODE_PWM ? configure_pwm(c, p)
                                    : configure_digital(c, p));
  s_gpio.configured |= 1ULL << pin;
  return NULL;
}

// The pin back to unconfigured. Every step runs; the first failure is returned.
static err_h release_pin(uint8_t pin) {
  LOCK();
  esp32s3_gpio_pin_t* p = esp32s3_gpio_pin(pin);
  if (p == NULL) {
    UNLOCK();
    return NULL;
  }
  const bool was_adc = p->pin_mode == SYS_IO_MODE_ADC;
  const adc_cali_handle_t cali = was_adc ? p->hw.adc.cali_handle : NULL;
  err_h err = NULL;
  // Under the lock: the PWM pools are shared by every PWM pin, and the ISR reads the slot until it is cleared.
  if (p->pin_mode == SYS_IO_MODE_PWM) {
    ERR_KEEP_FIRST(err, esp32s3_gpio_pwm_release(p));
  }
  if (is_digital(p->pin_mode) && p->intr.mode != SYS_IO_INTR_DISABLE) {
    ERR_KEEP_FIRST(err, ERR_ESP(gpio_isr_handler_remove((gpio_num_t)pin)));
  }
  s_gpio.configured &= ~(1ULL << pin);
  memset(p, 0, sizeof(*p));
  UNLOCK();

  ERR_KEEP_FIRST(err, ERR_ESP(gpio_reset_pin((gpio_num_t)pin)));
  if (was_adc) {
    ERR_KEEP_FIRST(err, ERR_ESP(adc_cali_delete_scheme_curve_fitting(cali)));
    ERR_KEEP_FIRST(err, esp32s3_gpio_adc_update_channels());
  }
  return err;
}

/* ---- IO contract ---- */

OP_WRITE(IO, MODE) {
  gpio_t* c = ctx;
  ERR_TRY(sys_pins_state_check_free(&c->pins, c->base.id, args->pin));
  LOCK();
  err_h err = configure_pin(c, args->pin, args->mode);
  UNLOCK();
  if (err == NULL && args->mode == SYS_IO_MODE_ADC) {
    err = esp32s3_gpio_adc_update_channels();  // without the lock: it takes it again
  }
  return err;
}

OP_WRITE(IO, RESET) {
  gpio_t* c = ctx;
  ERR_TRY(sys_pins_state_check_free(&c->pins, c->base.id, args->pin));
  return release_pin(args->pin);
}

#define LEVEL_TOGGLE (-1)

// Drives a push-pull / open-drain pin to `level` (0, 1 or LEVEL_TOGGLE). The caller has checked who may write it.
static err_h drive(const gpio_t* c, uint8_t pin, int level) {
  esp32s3_gpio_pin_t* p;
  LOCK();
  err_h err = pin_in_mode(c, pin, is_digital_output, &p);
  if (err == NULL) {
    const int to = level == LEVEL_TOGGLE ? gpio_get_level((gpio_num_t)pin) == 0 : level;
    err = ERR_ESP(gpio_set_level((gpio_num_t)pin, (uint32_t)to));
  }
  UNLOCK();
  return err;
}

OP_WRITE(IO, LEVEL) {
  gpio_t* c = ctx;
  ERR_CHECK(args->level <= 1, ERR_CMD_BAD_ARG);
  ERR_TRY(sys_pins_state_check_free(&c->pins, c->base.id, args->pin));
  return drive(c, args->pin, args->level);
}

OP_WRITE(IO, TOGGLE) {
  gpio_t* c = ctx;
  ERR_TRY(sys_pins_state_check_free(&c->pins, c->base.id, args->pin));
  return drive(c, args->pin, LEVEL_TOGGLE);
}

// A read: answered on a locked pin too.
OP_READ(IO, LEVEL) {
  const gpio_t* c = ctx;
  ERR_TRY(sys_pins_state_check(&c->pins, c->base.id, args->pin));
  esp32s3_gpio_pin_t* p;
  LOCK();
  err_h err = pin_in_mode(c, args->pin, is_digital, &p);
  const bool level = err == NULL && gpio_get_level((gpio_num_t)args->pin) != 0;
  UNLOCK();
  ERR_TRY(err);
  return sys_io_reply_level(args->pin, level, out, out_cap, out_len);
}

// The ADC task's latest filtered reading.
OP_READ(IO, VOLTAGE) {
  const gpio_t* c = ctx;
  ERR_TRY(sys_pins_state_check(&c->pins, c->base.id, args->pin));
  esp32s3_gpio_pin_t* p;
  LOCK();
  err_h err = pin_in_mode(c, args->pin, is_adc, &p);
  const int32_t mV = err == NULL ? p->hw.adc.last_mV : 0;
  UNLOCK();
  ERR_TRY(err);
  return sys_io_reply_voltage(args->pin, mV, out, out_cap, out_len);
}

/* ---- Pin interrupts ---- */

/* Edge of an armed digital pin. The ISR service is installed without ESP_INTR_FLAG_IRAM, so this runs with the
 * cache on and may call flash code; sys_event_publish defers the whole delivery to the event task. */
static void pin_isr(void* arg) {
  esp32s3_gpio_pin_t* pin = arg;
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
  const sys_event_t ev = {.source = s_gpio.device_id,
                          .event = rising ? IO_EV_RISING : IO_EV_FALLING,
                          .channel = pin->io_num,
                          .flags = rising ? IO_EV_RISING_FLAGS : IO_EV_FALLING_FLAGS,
                          .value = rising};
  err_h err = sys_event_publish(&ev);  // from an ISR always NULL: a drop is counted and reported by sys_event
  (void)err;
}

// Arms / disarms a digital pin's edge interrupt (lock held).
static err_h arm_digital(const gpio_t* c, esp32s3_gpio_pin_t* p, const sys_io_intr_cfg_t* cfg) {
  const gpio_num_t pin = (gpio_num_t)p->io_num;
  if (cfg->mode == SYS_IO_INTR_DISABLE) {
    p->intr = *cfg;
    err_h err = ERR_ESP(gpio_isr_handler_remove(pin));
    ERR_KEEP_FIRST(err, ERR_ESP(gpio_set_intr_type(pin, GPIO_INTR_DISABLE)));
    return err;
  }
  ERR_CHECK(cfg->mode < ARRAY_LEN(k_gpio_intr_map) && k_gpio_intr_map[cfg->mode] != GPIO_INTR_DISABLE,
            ERR_IO_FEATURE_UNSUPPORTED, .id = c->base.id, .pin = p->io_num);
  p->intr = *cfg;
  p->last_isr_us = 0;
  err_h err = ERR_ESP(gpio_set_intr_type(pin, k_gpio_intr_map[cfg->mode]));
  if (err == NULL) {
    err = ERR_ESP(gpio_isr_handler_add(pin, pin_isr, p));  // enables the interrupt; replaces an earlier handler
  }
  if (err != NULL) {
    p->intr.mode = SYS_IO_INTR_DISABLE;
    (void)gpio_isr_handler_remove(pin);  // only clears the slot; the cause above is what gets reported
  }
  return err;
}

// Arms / disarms an ADC pin's thresholds (lock held); the ADC task checks them on every reading.
static err_h arm_adc(const gpio_t* c, esp32s3_gpio_pin_t* p, const sys_io_intr_cfg_t* cfg) {
  ERR_CHECK(cfg->mode != SYS_IO_INTR_MODE_BOTH_EDGES, ERR_IO_FEATURE_UNSUPPORTED, .id = c->base.id, .pin = p->io_num);
  if (cfg->mode == SYS_IO_INTR_ADC_WINDOW_OUTSIDE || cfg->mode == SYS_IO_INTR_ADC_WINDOW_INSIDE) {
    SYS_DEV_CHECK_IN_RANGE(c->base.id, cfg->adc.threshold_down_mV, 0, cfg->adc.threshold_up_mV);
  }
  p->intr = *cfg;
  p->hw.adc.alert_active = false;
  p->hw.adc.past_count = 0;
  return NULL;
}

/* Digital pin (input or output): RISING / FALLING / BOTH edges from the ISR, value = level. ADC pin: RISING
 * (>= up), FALLING (<= down), ADC_OUTSIDE, ADC_INSIDE from the ADC task, value = mV. Events: IO_EVENTS,
 * channel = pin. Re-arming replaces the old config. */
OP_WRITE(IO, INTR) {
  gpio_t* c = ctx;
  ERR_CHECK(args->cfg.mode <= SYS_IO_INTR_ADC_WINDOW_INSIDE && args->cfg.debounce <= 1, ERR_CMD_BAD_ARG);
  ERR_TRY(sys_pins_state_check_free(&c->pins, c->base.id, args->pin));
  esp32s3_gpio_pin_t* p;
  LOCK();
  err_h err = pin_configured(c, args->pin, &p);
  if (err == NULL) {
    if (p->pin_mode == SYS_IO_MODE_ADC) {
      err = arm_adc(c, p, &args->cfg);
    } else if (is_digital(p->pin_mode)) {
      err = arm_digital(c, p, &args->cfg);
    } else {
      err = ERR_NEW(ERR_IO_WRONG_MODE, .id = c->base.id, .pin = args->pin, .mode = p->pin_mode);
    }
  }
  UNLOCK();
  return err;
}

OP_WRITE(IO, PWM_FREQ) {
  gpio_t* c = ctx;
  ERR_TRY(sys_pins_state_check_free(&c->pins, c->base.id, args->pin));
  SYS_DEV_CHECK_IN_RANGE(c->base.id, args->frequency_Hz, ESP32S3_GPIO_PWM_FREQ_MIN_HZ, ESP32S3_GPIO_PWM_FREQ_MAX_HZ);
  esp32s3_gpio_pin_t* p;
  LOCK();
  err_h err = pin_in_mode(c, args->pin, is_pwm, &p);
  if (err == NULL) {
    err = esp32s3_gpio_pwm_set_frequency(p, args->frequency_Hz);
  }
  UNLOCK();
  return err;
}

OP_WRITE(IO, PWM_DUTY) {
  gpio_t* c = ctx;
  ERR_TRY(sys_pins_state_check_free(&c->pins, c->base.id, args->pin));
  SYS_DEV_CHECK_IN_RANGE(c->base.id, args->duty, 0, IO_PWM_DUTY_MAX);
  esp32s3_gpio_pin_t* p;
  LOCK();
  err_h err = pin_in_mode(c, args->pin, is_pwm, &p);
  if (err == NULL) {
    err = esp32s3_gpio_pwm_set_duty(p, (uint16_t)args->duty);
  }
  UNLOCK();
  return err;
}

/* ---- Pins contract ---- */

SYS_PINS_STATE_HANDLERS(gpio_t)

// The owner of a locked pin (PCA9685 OE, a chip RST) drives it here; on an unlocked pin it is a plain write. A write:
// refused while suspended (the sweeps suspend the owners first and resume them last).
OP_WRITE(PINS, LOCKED_LEVEL) {
  gpio_t* c = ctx;
  ERR_TRY(sys_pins_state_check(&c->pins, c->base.id, args->pin));
  ERR_CHECK(args->level <= 1, ERR_CMD_BAD_ARG);
  if ((c->pins.locked & (1ULL << args->pin)) == 0) {
    ERR_TRY(sys_pins_state_check_free(&c->pins, c->base.id, args->pin));
  }
  return drive(c, args->pin, args->level);
}

/* ---- Lifecycle ---- */

// Releases every configured pin (locked ones too when `all`). Every pin is tried.
static err_h release_pins(const gpio_t* c, bool all) {
  LOCK();
  const uint64_t mask = s_gpio.configured & (all ? ~0ULL : ~c->pins.locked);
  UNLOCK();
  err_h err = NULL;
  for (uint8_t pin = 0; pin < GPIO_NUM_MAX; pin++) {
    if (mask & (1ULL << pin)) {
      ERR_KEEP_FIRST(err, release_pin(pin));
    }
  }
  return err;
}

// A pin locked by another device (TCA6424A's RST) is left to its owner: the sweep goes low -> high ID, so the
// owner's own reset still needs the pin configured.
OP_WRITE(SYS_DEVICE, RESET) {
  return release_pins(ctx, false);
}

/* Fault safe state of the pins that belong to the user: PWM at duty 0, push-pull outputs low, open-drain outputs
 * released. Inputs and ADC pins have nothing to make safe. A pin locked by another device is left to that device
 * (it makes it safe in its own SUSPEND). The state is kept for RESUME; writes are refused while suspended, so it
 * can't go stale. Every pin is reached even if one fails. */
static err_h suspend_hook(void* ctx) {
  gpio_t* c = ctx;
  err_h err = NULL;
  LOCK();
  for (uint8_t pin = 0; pin < GPIO_NUM_MAX; pin++) {
    esp32s3_gpio_pin_t* p = esp32s3_gpio_pin(pin);
    if (p == NULL || p->suspended || (c->pins.locked & (1ULL << pin))) {
      continue;
    }
    if (p->pin_mode == SYS_IO_MODE_PWM) {
      if (p->hw.pwm.duty == 0) {
        continue;  // already off (and possibly no timer bound yet)
      }
      p->hw.pwm.saved_duty = p->hw.pwm.duty;
      p->suspended = true;
      ERR_KEEP_FIRST(err, esp32s3_gpio_pwm_set_duty(p, 0));
    } else if (is_digital_output(p->pin_mode)) {
      p->saved_level = gpio_get_level((gpio_num_t)pin) != 0;
      p->suspended = true;
      // Open-drain: released (high); push-pull: low.
      ERR_KEEP_FIRST(err, ERR_ESP(gpio_set_level((gpio_num_t)pin, p->pin_mode != SYS_IO_MODE_OUTPUT_PUSH_PULL)));
    }
  }
  UNLOCK();
  return err;
}

// Brings back what SUSPEND saved: PWM duties and output levels. A pin that fails stays marked, and the device
// stays suspended, so RESUME can be repeated.
static err_h resume_hook(void* ctx) {
  (void)ctx;
  err_h err = NULL;
  LOCK();
  for (uint8_t pin = 0; pin < GPIO_NUM_MAX; pin++) {
    esp32s3_gpio_pin_t* p = esp32s3_gpio_pin(pin);
    if (p == NULL || !p->suspended) {
      continue;
    }
    err_h step = p->pin_mode == SYS_IO_MODE_PWM ? esp32s3_gpio_pwm_set_duty(p, p->hw.pwm.saved_duty)
                                                : ERR_ESP(gpio_set_level((gpio_num_t)pin, p->saved_level));
    if (step == NULL) {
      p->suspended = false;
    }
    ERR_KEEP_FIRST(err, step);
  }
  UNLOCK();
  return err;
}

/* ---- Create ---- */

// Every pin released (locked ones too): REMOVE and the create rollback.
static err_h uninstall(void* ctx) {
  gpio_t* c = ctx;
  err_h err = release_pins(c, true);
  if (s_gpio.dev == c) {
    s_gpio.dev = NULL;
  }
  return err;
}

// No create struct.
static err_h create(void* ctx, const void* cfg) {
  (void)cfg;
  gpio_t* c = ctx;
  ERR_CHECK(s_gpio.dev == NULL, ERR_DEV_ID_TAKEN, .id = s_gpio.dev->base.id);
  sys_pins_state_init(&c->pins, 0);
  c->pins.available = SOC_GPIO_VALID_GPIO_MASK;  // not contiguous: GPIO 22-25 don't exist on the S3

  // Once per boot: REMOVE leaves the service installed (a second install answers ESP_ERR_INVALID_STATE).
  const esp_err_t rc = gpio_install_isr_service(0);
  ERR_TRY_ESP(rc == ESP_ERR_INVALID_STATE ? ESP_OK : rc);
  ERR_TRY(esp32s3_gpio_adc_start());
  s_gpio.device_id = c->base.id;
  s_gpio.dev = c;
  return NULL;
}

/* ---- Type ---- */

// No SET_VOLTAGE (a DAC does that). OPS lists this table.
static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(SYS_DEVICE, RESET),
    OP_ENTRY(IO, LEVEL),       OP_ENTRY_W(IO, TOGGLE),  OP_ENTRY_W(IO, PWM_DUTY),
    OP_ENTRY_W(IO, PWM_FREQ),  OP_ENTRY_W(IO, MODE),    OP_ENTRY_W(IO, RESET),
    OP_ENTRY_R(IO, VOLTAGE),   OP_ENTRY_W(IO, INTR),
    SYS_PINS_STATE_ENTRIES,    OP_ENTRY_W(PINS, LOCKED_LEVEL),
};

SYS_DEVICE_TYPE_DEFINE(ESP32S3_GPIO, esp32s3_gpio_cfg_t, gpio_t, k_ops)
