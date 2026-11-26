/*
 * ESP32-S3 GPIO: the chip's own pins as one device, type 0x0040, on the IO contract: digital inputs and outputs, PWM
 * (LEDC, esp32s3_gpio_pwm.c), ADC (ADC1 and its task, esp32s3_gpio_adc.c), edge and threshold events. A pin's index is
 * its GPIO. One device per board: a second CREATE is refused. Nothing includes it: the rest of the firmware reaches it
 * by its ID and the IO contract's ops; esp32s3_gpio.h is private to this folder.
 */
#include <string.h>

#include "board_time.h"
#include "driver/gpio.h"
#include "esp32s3_gpio.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_private/esp_gpio_reserve.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "soc/usb_pins.h"
#include "sys_device_driver.h"
#include "sys_event.h"
#include "sys_io_driver.h"
#include "utils.h"

SYS_DEVICE_TYPE_DECLARE(ESP32S3_GPIO, 0x0040)

/** @brief CREATE's struct (wire format): none, the device takes no configuration. */
typedef cmd_none_t esp32s3_gpio_cfg_t;

#define DEBOUNCE_US ((uint64_t)CONFIG_ESP32S3_GPIO_DEBOUNCE_MS * 1000u)

typedef struct {
  uint8_t id;
  sys_io_pins_t pins;    // which pins exist, which are locked and by whom
  sys_device_res_t res;  // the pins and the instance, given back by the registry
} gpio_t;

#define SYS_DEVICE_CTX gpio_t

/* The pins' real state: shared with the ISR and the ADC task, so it is not in the device's struct. */
static struct {
  esp32s3_gpio_pin_t pin[GPIO_NUM_MAX];
  uint64_t configured;  // bit per pin: its slot is live
  gpio_t* dev;          // the instance, NULL while there is none
  uint8_t device_id;    // its ID: the source of the pin events
} s_gpio = {.device_id = SYS_DEVICE_ID_NONE};

/* Guards s_gpio against the ADC task. The device's ops are serialized by the registry already. */
U_MUTEX_DEFINE(gpio_mutex);

esp32s3_gpio_pin_t* esp32s3_gpio_pin(uint8_t pin) {
  return pin < GPIO_NUM_MAX && (s_gpio.configured & (1ULL << pin)) ? &s_gpio.pin[pin] : NULL;
}

void esp32s3_gpio_lock(void) {
  (void)xSemaphoreTake(gpio_mutex, portMAX_DELAY);
}

bool esp32s3_gpio_try_lock(TickType_t ticks) {
  return xSemaphoreTake(gpio_mutex, ticks) == pdTRUE;
}

void esp32s3_gpio_unlock(void) {
  (void)xSemaphoreGive(gpio_mutex);
}

uint8_t esp32s3_gpio_device_id(void) {
  return s_gpio.device_id;
}

/* Mode -> the fields of ESP-IDF's gpio_config_t. GPIO_MODE_DISABLE (0) marks a mode the table doesn't do, so a mode is
 * checked by one lookup. */
static const struct {
  gpio_mode_t mode;
  gpio_pullup_t pull_up;
  gpio_pulldown_t pull_down;
} k_gpio_mode_map[] = {
    [SYS_IO_MODE_INPUT]                    = {GPIO_MODE_INPUT,           GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE},
    [SYS_IO_MODE_INPUT_PULLUP]             = {GPIO_MODE_INPUT,           GPIO_PULLUP_ENABLE,  GPIO_PULLDOWN_DISABLE},
    [SYS_IO_MODE_INPUT_PULLDOWN]           = {GPIO_MODE_INPUT,           GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_ENABLE},
    [SYS_IO_MODE_OUTPUT_PUSH_PULL]         = {GPIO_MODE_INPUT_OUTPUT,    GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE},
    [SYS_IO_MODE_OUTPUT_OPEN_DRAIN]        = {GPIO_MODE_INPUT_OUTPUT_OD, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE},
    [SYS_IO_MODE_OUTPUT_OPEN_DRAIN_PULLUP] = {GPIO_MODE_INPUT_OUTPUT_OD, GPIO_PULLUP_ENABLE,  GPIO_PULLDOWN_DISABLE},
};

/* Interrupt mode -> ESP-IDF's interrupt type of a digital pin. GPIO_INTR_DISABLE (0) marks a mode a digital pin can't
 * do (the ADC window modes). */
static const gpio_int_type_t k_gpio_intr_map[] = {
    [SYS_IO_INTR_MODE_RISING_EDGE]  = GPIO_INTR_POSEDGE,
    [SYS_IO_INTR_MODE_FALLING_EDGE] = GPIO_INTR_NEGEDGE,
    [SYS_IO_INTR_MODE_BOTH_EDGES]   = GPIO_INTR_ANYEDGE,
};

// Inputs and outputs driven by the GPIO matrix: the pins whose edges the ISR sees.
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

// The pin's state (the lock of the pins held), or ERR_IO_PIN_UNCONFIGURED.
static err_h pin_configured(const gpio_t* c, uint8_t pin, esp32s3_gpio_pin_t** p) {
  *p = esp32s3_gpio_pin(pin);
  ERR_CHECK(*p != NULL, ERR_IO_PIN_UNCONFIGURED, .id = c->id, .pin = pin);
  return NULL;
}

// Like pin_configured, and the pin's mode must be one `mode_ok` takes, else ERR_IO_WRONG_MODE (the lock held).
static err_h pin_in_mode(const gpio_t* c, uint8_t pin, bool (*mode_ok)(sys_io_mode_e), esp32s3_gpio_pin_t** p) {
  ERR_TRY(pin_configured(c, pin, p));
  ERR_CHECK(mode_ok((*p)->pin_mode), ERR_IO_WRONG_MODE, .id = c->id, .pin = pin, .mode = (*p)->pin_mode);
  return NULL;
}

/* ---- A pin's setup and release ---- */

// ADC1 at 12 dB with curve-fitting calibration; the ADC task samples it once the channels are rebuilt.
static err_h configure_adc(const gpio_t* c, esp32s3_gpio_pin_t* p) {
  adc_unit_t unit = 0;
  ERR_CHECK(adc_continuous_io_to_channel(p->io_num, &unit, &p->hw.adc.channel) == ESP_OK && unit == ADC_UNIT_1,
            ERR_IO_FEATURE_UNSUPPORTED, .id = c->id, .pin = p->io_num);
  const adc_cali_curve_fitting_config_t cali = {
      .unit_id = ADC_UNIT_1,
      .chan = p->hw.adc.channel,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = SOC_ADC_DIGI_MAX_BITWIDTH,
  };
  return ERR_ESP(adc_cali_create_scheme_curve_fitting(&cali, &p->hw.adc.cali_handle));
}

/* A channel reserved, the pin held low until the first frequency or duty write binds a timer. Not gpio_config(): it
 * reserves an output pin, and LEDC then warns that the pin is taken. */
static err_h configure_pwm(const gpio_t* c, esp32s3_gpio_pin_t* p) {
  const gpio_num_t pin = (gpio_num_t)p->io_num;
  ERR_CHECK((SOC_GPIO_VALID_OUTPUT_GPIO_MASK & (1ULL << pin)) != 0, ERR_IO_MODE_UNSUPPORTED, .id = c->id,
            .pin = p->io_num, .mode = SYS_IO_MODE_PWM);
  ERR_TRY(esp32s3_gpio_pwm_claim(p));
  err_h err = ERR_ESP(gpio_set_level(pin, 0));
  if (err == NULL) {
    err = ERR_ESP(gpio_set_pull_mode(pin, GPIO_FLOATING));
  }
  if (err == NULL) {
    err = ERR_ESP(gpio_set_direction(pin, GPIO_MODE_OUTPUT));
  }
  if (err != NULL) {
    ERR_KEEP_FIRST(err, esp32s3_gpio_pwm_release(p));  // only frees the channel: no timer is bound yet
  }
  return err;
}

static err_h configure_digital(const gpio_t* c, const esp32s3_gpio_pin_t* p) {
  const uint8_t mode = p->pin_mode;
  ERR_CHECK(mode < ARRAY_LEN(k_gpio_mode_map) && k_gpio_mode_map[mode].mode != GPIO_MODE_DISABLE,
            ERR_IO_MODE_UNSUPPORTED, .id = c->id, .pin = p->io_num, .mode = mode);
  const gpio_config_t cfg = {
      .pin_bit_mask = (1ULL << p->io_num),
      .mode = k_gpio_mode_map[mode].mode,
      .pull_up_en = k_gpio_mode_map[mode].pull_up,
      .pull_down_en = k_gpio_mode_map[mode].pull_down,
      .intr_type = GPIO_INTR_DISABLE,
  };
  return ERR_ESP(gpio_config(&cfg));
}

/* An unconfigured pin put into `mode` (the lock of the pins held); its slot goes live only when that worked. A pin a
 * native peripheral holds (reserved in ESP-IDF since CREATE: flash, PSRAM, a bus started later) is no device's:
 * ERR_DEV_PIN_LOCKED with owner SYS_DEVICE_ID_NONE. */
static err_h configure_pin(const gpio_t* c, uint8_t pin, uint8_t mode) {
  const esp32s3_gpio_pin_t* existing = esp32s3_gpio_pin(pin);
  ERR_CHECK(existing == NULL, ERR_IO_PIN_IN_USE, .id = c->id, .pin = pin, .mode = existing->pin_mode);
  ERR_CHECK(!esp_gpio_is_reserved(BIT64(pin)), ERR_DEV_PIN_LOCKED, .id = c->id, .pin = pin,
            .owner = SYS_DEVICE_ID_NONE);
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
  esp32s3_gpio_lock();
  esp32s3_gpio_pin_t* p = esp32s3_gpio_pin(pin);
  if (p == NULL) {
    esp32s3_gpio_unlock();
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
  esp32s3_gpio_unlock();

  ERR_KEEP_FIRST(err, ERR_ESP(gpio_reset_pin((gpio_num_t)pin)));
  if (was_adc) {
    ERR_KEEP_FIRST(err, ERR_ESP(adc_cali_delete_scheme_curve_fitting(cali)));
    ERR_KEEP_FIRST(err, esp32s3_gpio_adc_update_channels());
  }
  return err;
}

// Every configured pin released, the locked ones too when `all`. Every pin is tried.
static err_h release_pins(const gpio_t* c, bool all) {
  esp32s3_gpio_lock();
  const uint64_t mask = s_gpio.configured & (all ? ~0ULL : ~c->pins.locked);
  esp32s3_gpio_unlock();
  err_h err = NULL;
  for (uint8_t pin = 0; pin < GPIO_NUM_MAX; pin++) {
    if (mask & (1ULL << pin)) {
      ERR_KEEP_FIRST(err, release_pin(pin));
    }
  }
  return err;
}

/* ---- IO contract ---- */

/*******************************************************************************
 * CONTRACT_IO : OP_MODE (write)
 * Configures a free/caller-locked pin's mode and updates ADC channels if needed.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_MODE) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  esp32s3_gpio_lock();
  err_h err = configure_pin(c, args->pin, args->mode);
  esp32s3_gpio_unlock();
  if (err == NULL && args->mode == SYS_IO_MODE_ADC) {
    err = esp32s3_gpio_adc_update_channels();  // without the lock: it takes it itself
  }
  return err;
}

/*******************************************************************************
 * CONTRACT_IO : OP_RESET (write)
 * Releases a single pin back to unconfigured state.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_RESET) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  return release_pin(args->pin);
}

#define LEVEL_TOGGLE (-1)

// A push-pull / open-drain pin driven to `level` (0, 1 or LEVEL_TOGGLE). The caller has checked who may write it.
static err_h drive(const gpio_t* c, uint8_t pin, int level) {
  esp32s3_gpio_pin_t* p;
  esp32s3_gpio_lock();
  err_h err = pin_in_mode(c, pin, is_digital_output, &p);
  if (err == NULL) {
    const int to = level == LEVEL_TOGGLE ? gpio_get_level((gpio_num_t)pin) == 0 : level;
    err = ERR_ESP(gpio_set_level((gpio_num_t)pin, (uint32_t)to));
  }
  esp32s3_gpio_unlock();
  return err;
}

/*******************************************************************************
 * CONTRACT_IO : OP_LEVEL (write)
 * Drives a digital output pin high or low.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_LEVEL) {
  ERR_CHECK(args->level <= 1, ERR_BAD_ARG);
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  return drive(c, args->pin, args->level);
}

/*******************************************************************************
 * CONTRACT_IO : OP_LEVEL (read)
 * Samples any configured digital pin (allowed on locked pins too).
 ******************************************************************************/
OP_DECLARE_READ(CONTRACT_IO, OP_LEVEL) {
  ERR_TRY(sys_io_pins_check(&c->pins, c->id, args->pin));
  esp32s3_gpio_pin_t* p;
  esp32s3_gpio_lock();
  err_h err = pin_in_mode(c, args->pin, is_digital, &p);
  const bool level = err == NULL && gpio_get_level((gpio_num_t)args->pin) != 0;
  esp32s3_gpio_unlock();
  ERR_TRY(err);
  return CMD_REPLY(CONTRACT_IO, OP_LEVEL, .pin = args->pin, .level = level);
}

/*******************************************************************************
 * CONTRACT_IO : OP_TOGGLE (write)
 * Inverts a digital output pin's current logic level.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_TOGGLE) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  return drive(c, args->pin, LEVEL_TOGGLE);
}

/*******************************************************************************
 * CONTRACT_IO : OP_VOLTAGE (read)
 * Returns the ADC task's latest filtered voltage reading in mV.
 ******************************************************************************/
OP_DECLARE_READ(CONTRACT_IO, OP_VOLTAGE) {
  ERR_TRY(sys_io_pins_check(&c->pins, c->id, args->pin));
  esp32s3_gpio_pin_t* p;
  esp32s3_gpio_lock();
  err_h err = pin_in_mode(c, args->pin, is_adc, &p);
  const int32_t mV = err == NULL ? p->hw.adc.last_mV : 0;
  esp32s3_gpio_unlock();
  ERR_TRY(err);
  return CMD_REPLY(CONTRACT_IO, OP_VOLTAGE, .pin = args->pin, .voltage_mV = mV);
}

/* ---- Pin interrupts ---- */

/* An edge of an armed digital pin. The ISR service is installed without ESP_INTR_FLAG_IRAM, so this runs with the
 * cache on and may call flash code. A publish from an ISR is always deferred to the event task, and a drop there is
 * counted and reported by it: the publish returns NULL. */
static void pin_isr(void* arg) {
  esp32s3_gpio_pin_t* pin = arg;
  const uint8_t mode = pin->intr.mode;
  if (mode == SYS_IO_INTR_DISABLE) {
    return;
  }
  // Only switches are debounced. A chip's alert line (debounce off) holds a latched flag: an edge dropped here would
  // never be followed by another, so the flag would stay set and the line stuck asserted.
  if (pin->intr.debounce) {
    const uint64_t now_us = board_time_uptime_us();
    if (now_us - pin->last_isr_us < DEBOUNCE_US) {
      return;
    }
    pin->last_isr_us = now_us;
  }
  // A one-edge mode publishes its own edge (the level may have bounced back already); both edges: the level now.
  const bool rising = mode == SYS_IO_INTR_MODE_RISING_EDGE ||
                      (mode == SYS_IO_INTR_MODE_BOTH_EDGES && gpio_get_level((gpio_num_t)pin->io_num) != 0);
  err_h err = rising ? SYS_EVENT_PUBLISH(IO_EV_RISING, s_gpio.device_id, pin->io_num)
                     : SYS_EVENT_PUBLISH(IO_EV_FALLING, s_gpio.device_id, pin->io_num);
  (void)err;
}

// A digital pin's edge interrupt armed or disarmed (the lock of the pins held).
static err_h arm_digital(const gpio_t* c, esp32s3_gpio_pin_t* p, const sys_io_intr_cfg_t* cfg) {
  const gpio_num_t pin = (gpio_num_t)p->io_num;
  if (cfg->mode == SYS_IO_INTR_DISABLE) {
    p->intr = *cfg;
    err_h err = ERR_ESP(gpio_isr_handler_remove(pin));
    ERR_KEEP_FIRST(err, ERR_ESP(gpio_set_intr_type(pin, GPIO_INTR_DISABLE)));
    return err;
  }
  ERR_CHECK(cfg->mode < ARRAY_LEN(k_gpio_intr_map) && k_gpio_intr_map[cfg->mode] != GPIO_INTR_DISABLE,
            ERR_IO_FEATURE_UNSUPPORTED, .id = c->id, .pin = p->io_num);
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

// An ADC pin's thresholds armed or disarmed (the lock of the pins held); the ADC task checks them on every reading.
static err_h arm_adc(const gpio_t* c, esp32s3_gpio_pin_t* p, const sys_io_intr_cfg_t* cfg) {
  ERR_CHECK(cfg->mode != SYS_IO_INTR_MODE_BOTH_EDGES, ERR_IO_FEATURE_UNSUPPORTED, .id = c->id, .pin = p->io_num);
  if (cfg->mode == SYS_IO_INTR_ADC_WINDOW_OUTSIDE || cfg->mode == SYS_IO_INTR_ADC_WINDOW_INSIDE) {
    ERR_CHECK_RANGE(cfg->adc.threshold_down_mV, 0, cfg->adc.threshold_up_mV);
  }
  p->intr = *cfg;
  p->hw.adc.alert_active = false;
  p->hw.adc.past_count = 0;
  return NULL;
}

/*******************************************************************************
 * CONTRACT_IO : OP_INTR (write)
 * Arms digital edge (ISR) or ADC threshold/window events on a pin (`IO_EVENTS`,
 * channel = pin). Re-arming replaces the previous configuration.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_INTR) {
  ERR_CHECK(args->cfg.mode <= SYS_IO_INTR_ADC_WINDOW_INSIDE && args->cfg.debounce <= 1, ERR_BAD_ARG);
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  esp32s3_gpio_pin_t* p;
  esp32s3_gpio_lock();
  err_h err = pin_configured(c, args->pin, &p);
  if (err == NULL) {
    if (p->pin_mode == SYS_IO_MODE_ADC) {
      err = arm_adc(c, p, &args->cfg);
    } else if (is_digital(p->pin_mode)) {
      err = arm_digital(c, p, &args->cfg);
    } else {
      err = ERR_NEW(ERR_IO_WRONG_MODE, .id = c->id, .pin = args->pin, .mode = p->pin_mode);
    }
  }
  esp32s3_gpio_unlock();
  return err;
}

/*******************************************************************************
 * CONTRACT_IO : OP_PWM_FREQ (write)
 * Sets the LEDC timer frequency for a PWM pin.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_PWM_FREQ) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  ERR_CHECK_RANGE(args->frequency_Hz, ESP32S3_GPIO_PWM_FREQ_MIN_HZ, ESP32S3_GPIO_PWM_FREQ_MAX_HZ);
  esp32s3_gpio_pin_t* p;
  esp32s3_gpio_lock();
  err_h err = pin_in_mode(c, args->pin, is_pwm, &p);
  if (err == NULL) {
    err = esp32s3_gpio_pwm_set_frequency(p, args->frequency_Hz);
  }
  esp32s3_gpio_unlock();
  return err;
}

/*******************************************************************************
 * CONTRACT_IO : OP_PWM_DUTY (write)
 * Sets the LEDC PWM duty cycle (0..IO_PWM_DUTY_MAX) for a PWM pin.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_PWM_DUTY) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  ERR_CHECK_RANGE(args->duty, 0, IO_PWM_DUTY_MAX);
  esp32s3_gpio_pin_t* p;
  esp32s3_gpio_lock();
  err_h err = pin_in_mode(c, args->pin, is_pwm, &p);
  if (err == NULL) {
    err = esp32s3_gpio_pwm_set_duty(p, (uint16_t)args->duty);
  }
  esp32s3_gpio_unlock();
  return err;
}

/*******************************************************************************
 * CONTRACT_IO : OP_AVAILABLE / OP_LOCKED / OP_LOCK / OP_UNLOCK
 * Shared pin availability and ownership handlers; owners drive locked pins via
 * the IO ops above.
 ******************************************************************************/
SYS_IO_PINS_HANDLERS

/* ---- Lifecycle ---- */

/*******************************************************************************
 * CONTRACT_SYS_DEVICE : OP_RESET (write)
 * Releases user-configured pins while preserving pins locked by other devices.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  return release_pins(c, false);
}

/*******************************************************************************
 * ESP32S3_GPIO : suspend (lifecycle)
 * Puts every PWM (duty 0) and the unlocked digital outputs (push-pull low,
 * open-drain released) into safe state while saving values for resume. A PWM
 * goes off also when another device holds its pin: this chip generates it, and
 * once it is suspended the owner's writes are refused. A locked digital output
 * stays with its owner.
 ******************************************************************************/
static err_h suspend(gpio_t* c) {
  err_h err = NULL;
  esp32s3_gpio_lock();
  for (uint8_t pin = 0; pin < GPIO_NUM_MAX; pin++) {
    esp32s3_gpio_pin_t* p = esp32s3_gpio_pin(pin);
    if (p == NULL || p->suspended) {
      continue;
    }
    if (p->pin_mode == SYS_IO_MODE_PWM) {
      if (p->hw.pwm.duty == 0) {
        continue;  // off already (and maybe no timer bound yet)
      }
      p->hw.pwm.saved_duty = p->hw.pwm.duty;
      p->suspended = true;
      ERR_KEEP_FIRST(err, esp32s3_gpio_pwm_set_duty(p, 0));
    } else if (is_digital_output(p->pin_mode) && (c->pins.locked & (1ULL << pin)) == 0) {
      p->saved_level = gpio_get_level((gpio_num_t)pin) != 0;
      p->suspended = true;
      // Open-drain: released (high); push-pull: low.
      ERR_KEEP_FIRST(err, ERR_ESP(gpio_set_level((gpio_num_t)pin, p->pin_mode != SYS_IO_MODE_OUTPUT_PUSH_PULL)));
    }
  }
  esp32s3_gpio_unlock();
  return err;
}

/*******************************************************************************
 * ESP32S3_GPIO : resume (lifecycle)
 * Restores PWM duties and output levels saved by suspend; failing pins remain
 * marked suspended so resume can be retried.
 ******************************************************************************/
static err_h resume(gpio_t* c) {
  err_h err = NULL;
  esp32s3_gpio_lock();
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
  esp32s3_gpio_unlock();
  return err;
}

/* ---- Create ---- */

/* The device's resource: every configured pin back to unconfigured, the locked ones too, and the instance let go. The
 * devices that locked pins are gone by then (REMOVE is refused while one uses this device). The ISR service and the ADC
 * task stay: they are set up once per boot. */
static err_h instance_release(void* arg) {
  gpio_t* c = arg;
  if (s_gpio.dev != c) {
    return NULL;  // create() failed before it took the instance
  }
  err_h err = release_pins(c, true);
  s_gpio.dev = NULL;
  s_gpio.device_id = SYS_DEVICE_ID_NONE;
  return err;
}

/*******************************************************************************
 * ESP32S3_GPIO : create (init)
 * Registers singleton instance resource, builds valid S3 pin mask, locks
 * reserved native/USB-JTAG pins, installs ISR service and starts ADC task.
 ******************************************************************************/
static err_h create(uint8_t id, gpio_t* c, const esp32s3_gpio_cfg_t* cfg) {
  ERR_CHECK(s_gpio.dev == NULL, ERR_DEV_ID_TAKEN, .id = s_gpio.device_id);
  c->id = id;
  ERR_TRY(sys_device_res_add(id, &c->res, instance_release, c));
  sys_io_pins_init(&c->pins, 0);
  c->pins.available = SOC_GPIO_VALID_GPIO_MASK;  // not contiguous: GPIO 22-25 don't exist on the S3
#if CONFIG_USJ_ENABLE_USB_SERIAL_JTAG
  // The USB-Serial-JTAG holds D- / D+ without reserving them in ESP-IDF: reserved here for it.
  (void)esp_gpio_reserve(BIT64(USBPHY_DM_NUM) | BIT64(USBPHY_DP_NUM));
#endif
  // The pins a native peripheral holds (flash, PSRAM, USB) are locked for no device (owner SYS_DEVICE_ID_NONE): no one
  // writes them. MODE checks the reservation again, for a peripheral started later.
  for (uint8_t pin = 0; pin < GPIO_NUM_MAX; pin++) {
    if ((c->pins.available & BIT64(pin)) && esp_gpio_is_reserved(BIT64(pin))) {
      c->pins.locked |= BIT64(pin);
    }
  }

  // Once per boot: REMOVE leaves the service installed (a second install answers ESP_ERR_INVALID_STATE).
  const esp_err_t rc = gpio_install_isr_service(0);
  ERR_TRY_ESP(rc == ESP_ERR_INVALID_STATE ? ESP_OK : rc);
  ERR_TRY(esp32s3_gpio_adc_start());
  s_gpio.device_id = id;
  s_gpio.dev = c;
  return NULL;
}

/*******************************************************************************
 * ESP32S3_GPIO : type registration (k_ops & SYS_DEVICE_TYPE_DEFINE)
 * Exposes lifecycle reset and IO contract + shared pin handlers (no VOLTAGE
 * write and no own ops).
 ******************************************************************************/

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),
    OP_ENTRY(CONTRACT_IO, OP_LEVEL),       OP_ENTRY_W(CONTRACT_IO, OP_TOGGLE),  OP_ENTRY_W(CONTRACT_IO, OP_PWM_DUTY),
    OP_ENTRY_W(CONTRACT_IO, OP_PWM_FREQ),  OP_ENTRY_W(CONTRACT_IO, OP_MODE),    OP_ENTRY_W(CONTRACT_IO, OP_RESET),
    OP_ENTRY_R(CONTRACT_IO, OP_VOLTAGE),   OP_ENTRY_W(CONTRACT_IO, OP_INTR),
    SYS_IO_PINS_ENTRIES,
};

SYS_DEVICE_TYPE_DEFINE(ESP32S3_GPIO, esp32s3_gpio_cfg_t, create, suspend, resume, k_ops)
