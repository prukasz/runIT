/*
 * Test device, type 0x004E: one device that has every contract and answers like a real one, for the tests of the
 * firmware and of the links between boards. The whole device is this file: nothing includes it, the rest of the
 * firmware reaches it by its ID, the contracts' ops and its own ops (0xFFxx).
 *
 * It has no hardware: what a real driver would read from a chip is state here, set by its own FAKE ops, and every
 * change of its state is logged (ESP_LOG, tag "testdev") and published as the event a real driver would publish.
 *   IO       `pins` pins (1..64): modes, levels, PWM, ADC and DAC voltages, interrupts (edges, ADC window), locks.
 *            An input reads what FAKE_PIN set; an output reads what was written.
 *   I2C      a simulated bus: chips with a 256-byte register file each (pointer first, then data, as most chips do),
 *            ATTACH / DETACH / XFER (LOCAL, so another driver can be created on this bus) and PROBE / SCAN / RAW.
 *            FAKE_I2C adds or removes a chip; the fault I2C_STUCK holds the bus until RECOVER.
 *   PMON     `channels` channels (1..4): voltage and current as FAKE_CHANNEL set them, power as their product, the
 *            current alerts (OCP_WARNING / OCP_CRITICAL when the current crosses the armed threshold).
 *   VREG     one regulator: the settings kept (READY when it is enabled or set higher while on); OP_VREG reads them.
 *   PD       a sink offering four profiles (5, 9, 15 V and a PPS); SETTINGS kept, LIMITS the highest offer.
 *   SUPPLY   the source kept, switched at once.
 *   HBRIDGE  `channels` channels: DRIVE / BRAKE / COAST kept; the current follows the drive (|drive| in mA / 4.095)
 *            unless FAKE_CHANNEL set it.
 *   UART     a port with an RX queue (4 messages of up to 64 bytes): SEND and TRANSACT come back as an RX message
 *            while LOOPBACK is on (the default), FAKE_UART_RX puts a message there; one reader at a time (a
 *            subscriber, a hook, or the call of RECEIVE / TRANSACT). A message for a subscriber goes through
 *            sys_device_deliver on the task that made it: a subscriber that sends from its own op gets ERR_DEV_REENTRY.
 *   EVENT    ON_EVENT counts the events it is given and keeps the last (OP_HEARD); OP_LISTEN subscribes it.
 * SUSPEND is the safe state of a real device: outputs low, PWM and DAC 0, bridges coasting, the regulator off, the
 * supply off. RESUME restores nothing. RESET returns every setting to its default.
 */
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "sys_device_driver.h"
#include "sys_device_event.h"
#include "sys_event.h"
#include "sys_hbridge.h"
#include "sys_i2c.h"
#include "sys_io_driver.h"
#include "sys_power_monitor.h"
#include "sys_power_supply.h"
#include "sys_power_usb_pd.h"
#include "sys_power_vreg.h"
#include "sys_uart_contract.h"
#include "utils.h"

SYS_DEVICE_TYPE_DECLARE(TESTDEV, 0x004E)

static const char* const TAG = "testdev";

/*******************************************************************************
 * TESTDEV : create struct and own operations (0xFFxx)
 ******************************************************************************/

#define TESTDEV_CHANNELS_MAX   4u
#define TESTDEV_I2C_CHIPS      4u
#define TESTDEV_UART_QUEUE     4u
#define TESTDEV_MSG_MAX        64u
#define TESTDEV_DAC_MAX_MV     3300
#define TESTDEV_VREG_MIN_MV    1000
#define TESTDEV_VREG_MAX_MV    20000
#define TESTDEV_VREG_MAX_MA    5000
#define TESTDEV_PD_MIN_MV      3300
#define TESTDEV_PD_MAX_MV      20000
#define TESTDEV_PD_MAX_MA      5000
#define TESTDEV_PWM_FREQ_MAX   1000000
#define PIN_UNCONFIGURED       0xFFu

/** @brief CREATE's struct (wire format). */
typedef struct __attribute__((packed)) {
  uint8_t pins;       /**< IO pins, 1..64 */
  uint8_t channels;   /**< Channels of the power monitor and the H-bridge, 1..4 */
  uint8_t uart_mode;  /**< sys_uart_mode_e: what the UART port says INFO */
  uint32_t uart_baud; /**< Its baud rate, SYS_UART_BAUD_MIN..MAX */
} testdev_cfg_t;

/** @brief FAKE_PIN's value, both sides: what the outside holds a pin at. The level is read by an input and says what
 * an output is held at; the voltage is what an ADC pin reads (a DAC pin's is the one written). */
typedef struct __attribute__((packed)) {
  uint8_t pin;        /**< Pin index */
  uint8_t level;      /**< 0 or 1 */
  int32_t voltage_mV; /**< ADC reading */
} testdev_pin_t;

/** @brief FAKE_I2C's value: a chip on the simulated bus. */
typedef struct __attribute__((packed)) {
  uint8_t addr;     /**< 7-bit address, 0x08..0x77 */
  uint8_t present;  /**< 1: a chip acks it (a register file of zeros when it is new); 0: nothing does */
} testdev_i2c_t;

/** @brief FAKE_CHANNEL's value, both sides: what a channel measures (the power monitor's and the bridge's current). */
typedef struct __attribute__((packed)) {
  uint8_t channel;    /**< Channel index */
  int32_t voltage_mV; /**< Bus voltage */
  int32_t current_mA; /**< Current */
} testdev_channel_t;

/** @brief What FAKE_FAULT injects: the event a real driver publishes, and the bus fault. */
typedef enum {
  TESTDEV_FAULT_IO_RISING = 0,       // IO_EV_RISING on pin `channel`
  TESTDEV_FAULT_IO_FALLING = 1,      // IO_EV_FALLING on pin `channel`
  TESTDEV_FAULT_PMON_WARNING = 2,    // PMON_EV_OCP_WARNING, `value` mA
  TESTDEV_FAULT_PMON_CRITICAL = 3,   // PMON_EV_OCP_CRITICAL, `value` mA
  TESTDEV_FAULT_VREG_OVP = 4,        // VREG_EV_OVP, `value` mV
  TESTDEV_FAULT_VREG_OCP = 5,        // VREG_EV_OCP, `value` mA
  TESTDEV_FAULT_VREG_SCP = 6,        // VREG_EV_SCP
  TESTDEV_FAULT_HB_OVERCURRENT = 7,  // HBRIDGE_EV_OVERCURRENT, `value` mA
  TESTDEV_FAULT_HB_THERMAL = 8,      // HBRIDGE_EV_THERMAL_OR_UVLO, `value` mA
  TESTDEV_FAULT_HB_EXTERNAL = 9,     // HBRIDGE_EV_EXTERNAL, `value` mA
  TESTDEV_FAULT_UART_OVERFLOW = 10,  // UART_EV_OVERFLOW, `value` bytes dropped
  TESTDEV_FAULT_I2C_STUCK = 11,      // The bus holds every transfer (ERR_I2C_BUS_STUCK) until RECOVER
} testdev_fault_e;

/** @brief FAKE_FAULT's value. */
typedef struct __attribute__((packed)) {
  uint8_t kind;       /**< testdev_fault_e */
  uint8_t channel;    /**< The event's channel: a pin or a measured channel (0 for the others) */
  int32_t value;      /**< The event's payload */
} testdev_fault_t;

/** @brief FAKE_UART_RX's head: then the message bytes (TAIL), 1..64. */
typedef struct __attribute__((packed)) {
  uint8_t copies;  /**< How many times the message arrives, 1..4 */
} testdev_rx_t;

/** @brief LISTEN's value: the events ON_EVENT hears (a key as in sys_event_key_t; SYS_EVENT_ANY_* match any). */
typedef struct __attribute__((packed)) {
  uint32_t tag;
  uint16_t source;
  uint16_t channel;
} testdev_listen_t;

/** @brief HEARD's answer: what ON_EVENT has been given. */
typedef struct __attribute__((packed)) {
  uint32_t count;       /**< Events heard since the last LISTEN (or RESET) */
  sys_event_key_t key;  /**< The last one */
  uint16_t len;         /**< Its payload bytes */
  uint8_t data[16];     /**< The first of them */
} testdev_heard_t;

/** @brief VREG's answer: the regulator's settings. */
typedef struct __attribute__((packed)) {
  uint8_t enable;
  uint32_t voltage_mV;
  uint32_t current_mA;
} testdev_vreg_t;

/** @brief LOOPBACK's value, both sides. */
typedef struct __attribute__((packed)) {
  uint8_t loopback;  /**< 1: what the UART sends comes back as a message */
} testdev_loopback_t;

#define OWN_TESTDEV_OPERATIONS(X, P)                                                                         \
  X(P, OP_FAKE_PIN,     0x00, io_pin_args_t,      testdev_pin_t,      SYS_DEVICE_OP_ALWAYS)                  \
  X(P, OP_FAKE_I2C,     0x01, cmd_none_t,         testdev_i2c_t,      SYS_DEVICE_OP_ALWAYS)                  \
  X(P, OP_FAKE_CHANNEL, 0x02, pmon_channel_args_t, testdev_channel_t, SYS_DEVICE_OP_ALWAYS)                  \
  X(P, OP_FAKE_FAULT,   0x03, cmd_none_t,         testdev_fault_t,    SYS_DEVICE_OP_ALWAYS)                  \
  X(P, OP_FAKE_UART_RX, 0x04, cmd_none_t,         testdev_rx_t,       SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_TAIL) \
  X(P, OP_LISTEN,       0x05, cmd_none_t,         testdev_listen_t,   0)                                     \
  X(P, OP_HEARD,        0x06, cmd_none_t,         testdev_heard_t,    0)                                     \
  X(P, OP_VREG,         0x07, cmd_none_t,         testdev_vreg_t,     0)                                     \
  X(P, OP_LOOPBACK,     0x08, cmd_none_t,         testdev_loopback_t, 0)

SYS_DEVICE_OWN_DECLARE(OWN_TESTDEV)

/* ----------------------------------------------------------------------------
 * The device's state
 * ------------------------------------------------------------------------- */

typedef struct {
  uint8_t mode;          // sys_io_mode_e, PIN_UNCONFIGURED before MODE
  uint8_t level;
  bool inside;           // the ADC reading is inside the interrupt's window
  uint32_t duty;
  uint32_t freq_Hz;
  int32_t mV;            // an ADC pin's reading, a DAC pin's output
  sys_io_intr_cfg_t intr;
} tpin_t;

typedef struct {
  int32_t mV;
  int32_t mA;
  int16_t drive;
  uint8_t bridge;        // HBRIDGE_DRIVE, _BRAKE, _COAST
  struct {
    bool armed;
    bool over;
    int32_t threshold_mA;
  } alert[2];            // sys_power_alert_e
} tch_t;

typedef struct {
  uint8_t addr;          // 0: a free slot
  uint8_t ptr;           // the register the next byte goes to or comes from
  uint8_t regs[256];
} tchip_t;

typedef struct {
  uint8_t len;
  uint8_t data[TESTDEV_MSG_MAX];
} tmsg_t;

typedef struct {
  uint8_t id;
  testdev_cfg_t cfg;
  // IO
  sys_io_pins_t pins;
  tpin_t pin[SYS_IO_PINS_MAX];
  // I2C
  tchip_t chip[TESTDEV_I2C_CHIPS];
  bool attached[SYS_I2C_ADDR_MAX + 1u];
  uint8_t owner[SYS_I2C_ADDR_MAX + 1u];  // of an attached address: the device that attached it
  bool stuck;
  // PMON, HBRIDGE
  tch_t ch[TESTDEV_CHANNELS_MAX];
  // VREG, PD, SUPPLY
  testdev_vreg_t vreg;
  pd_settings_args_t pd;
  uint8_t source;
  // UART
  uint32_t baud;
  uint8_t loopback;
  portMUX_TYPE rx_lock;  // the queue: SEND, RECEIVE and TRANSACT are NOLOCK ops, so two tasks can be in it
  tmsg_t rx[TESTDEV_UART_QUEUE];
  uint8_t rx_head;
  uint8_t rx_count;
  uint8_t reader;        // a subscriber (SYS_DEVICE_ID_NONE: none), its generation and op
  uint8_t reader_gen;
  uint16_t reader_op;
  sys_uart_rx_cb_f hook;
  void* hook_ctx;
  // EVENT
  sys_device_res_t sub_res;
  uint16_t sub;          // sys_event handle of LISTEN, 0 when none
  testdev_heard_t heard;
} testdev_t;

#define SYS_DEVICE_CTX testdev_t

static const sys_power_usb_pd_option_t k_pd_options[] = {
    {.slot = 1, .type = SYS_POWER_USB_PD_OPTION_FIXED, .min_mV = 5000, .max_mV = 5000, .max_mA = 3000},
    {.slot = 2, .type = SYS_POWER_USB_PD_OPTION_FIXED, .min_mV = 9000, .max_mV = 9000, .max_mA = 3000},
    {.slot = 3, .type = SYS_POWER_USB_PD_OPTION_FIXED, .min_mV = 15000, .max_mV = 15000, .max_mA = 3000},
    {.slot = 4, .type = SYS_POWER_USB_PD_OPTION_PPS, .min_mV = 3300, .max_mV = 11000, .max_mA = 3000},
};

/* ---- Defaults ---- */

static void rx_flush(testdev_t* c);

static void reset_state(testdev_t* c) {
  memset(c->pin, 0, sizeof(c->pin));
  for (uint32_t i = 0; i < SYS_IO_PINS_MAX; i++) {
    c->pin[i].mode = PIN_UNCONFIGURED;
    c->pin[i].inside = true;
  }
  memset(c->ch, 0, sizeof(c->ch));
  for (uint32_t i = 0; i < TESTDEV_CHANNELS_MAX; i++) {
    c->ch[i].mV = 5000 + (int32_t)i * 1000;  // fixed readings: 5, 6, 7, 8 V, 0 mA
    c->ch[i].bridge = 2;                     // coasting
  }
  memset(c->chip, 0, sizeof(c->chip));
  static const uint8_t k_chips[TESTDEV_I2C_CHIPS] = {0x20, 0x40, 0x48, 0x50};
  for (uint32_t i = 0; i < TESTDEV_I2C_CHIPS; i++) {
    c->chip[i].addr = k_chips[i];
  }
  c->stuck = false;
  c->vreg = (testdev_vreg_t){.enable = 0, .voltage_mV = 5000, .current_mA = 1000};
  c->pd = (pd_settings_args_t){.voltage_mV = 5000, .current_mA = 1000};
  c->source = SYS_POWER_SUPPLY_OFF;
  c->baud = c->cfg.uart_baud;
  c->loopback = 1;
  rx_flush(c);
  memset(&c->heard, 0, sizeof(c->heard));
}

/* ----------------------------------------------------------------------------
 * IO
 * ------------------------------------------------------------------------- */

static bool is_in(uint8_t m) { return m <= SYS_IO_MODE_INPUT_PULLDOWN; }
static bool is_out(uint8_t m) { return m >= SYS_IO_MODE_OUTPUT_PUSH_PULL && m <= SYS_IO_MODE_OUTPUT_OPEN_DRAIN_PULLUP; }
static bool is_digital(uint8_t m) { return is_in(m) || is_out(m); }

static err_h pin_mode_is(const testdev_t* c, uint8_t pin, bool (*ok)(uint8_t), const tpin_t** out) {
  const tpin_t* p = &c->pin[pin];
  ERR_CHECK(p->mode != PIN_UNCONFIGURED, ERR_IO_PIN_UNCONFIGURED, .id = c->id, .pin = pin);
  ERR_CHECK(ok(p->mode), ERR_IO_WRONG_MODE, .id = c->id, .pin = pin, .mode = p->mode);
  *out = p;
  return NULL;
}
static bool is_pwm(uint8_t m) { return m == SYS_IO_MODE_PWM; }
static bool is_adc(uint8_t m) { return m == SYS_IO_MODE_ADC; }
static bool is_dac(uint8_t m) { return m == SYS_IO_MODE_DAC; }
static bool is_analog(uint8_t m) { return is_adc(m) || is_dac(m); }

// The pin's level changes (by a write, by FAKE_PIN): logged, and an armed edge is published.
static err_h pin_set_level(testdev_t* c, uint8_t pin, uint8_t level) {
  tpin_t* p = &c->pin[pin];
  if (p->level == level) {
    return NULL;
  }
  p->level = level;
  ESP_LOGI(TAG, "dev %u pin %u level %u", c->id, pin, level);
  if (level != 0 && (p->intr.mode & SYS_IO_INTR_MODE_RISING_EDGE) && p->intr.mode <= SYS_IO_INTR_MODE_BOTH_EDGES) {
    return SYS_EVENT_PUBLISH(IO_EV_RISING, c->id, pin);
  }
  if (level == 0 && (p->intr.mode & SYS_IO_INTR_MODE_FALLING_EDGE) && p->intr.mode <= SYS_IO_INTR_MODE_BOTH_EDGES) {
    return SYS_EVENT_PUBLISH(IO_EV_FALLING, c->id, pin);
  }
  return NULL;
}

static bool in_window(const tpin_t* p) {
  return p->mV <= (int32_t)p->intr.adc.threshold_up_mV && p->mV >= (int32_t)p->intr.adc.threshold_down_mV;
}

// An ADC reading changes: logged, and entering or leaving an armed window is published.
static err_h pin_set_mV(testdev_t* c, uint8_t pin, int32_t mV) {
  tpin_t* p = &c->pin[pin];
  if (p->mV == mV) {
    return NULL;
  }
  p->mV = mV;
  ESP_LOGI(TAG, "dev %u pin %u voltage %ld mV", c->id, pin, (long)mV);
  if (p->mode != SYS_IO_MODE_ADC || p->intr.mode < SYS_IO_INTR_ADC_WINDOW_OUTSIDE) {
    return NULL;
  }
  const bool inside = in_window(p);
  const bool changed = inside != p->inside;
  p->inside = inside;
  if (changed && !inside && p->intr.mode == SYS_IO_INTR_ADC_WINDOW_OUTSIDE) {
    return SYS_EVENT_PUBLISH(IO_EV_ADC_OUTSIDE, c->id, pin, .mV = mV);
  }
  if (changed && inside && p->intr.mode == SYS_IO_INTR_ADC_WINDOW_INSIDE) {
    return SYS_EVENT_PUBLISH(IO_EV_ADC_INSIDE, c->id, pin, .mV = mV);
  }
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_IO, OP_MODE) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  tpin_t* p = &c->pin[args->pin];
  ERR_CHECK(args->mode <= SYS_IO_MODE_DAC, ERR_IO_MODE_UNSUPPORTED, .id = c->id, .pin = args->pin, .mode = args->mode);
  ERR_CHECK(p->mode == PIN_UNCONFIGURED, ERR_IO_PIN_IN_USE, .id = c->id, .pin = args->pin, .mode = p->mode);
  memset(p, 0, sizeof(*p));
  p->mode = args->mode;
  p->inside = true;
  p->level = args->mode == SYS_IO_MODE_INPUT_PULLUP ? 1 : 0;  // an input reads its pull until FAKE_PIN says otherwise
  ESP_LOGI(TAG, "dev %u pin %u mode %u", c->id, args->pin, args->mode);
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_IO, OP_RESET) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  memset(&c->pin[args->pin], 0, sizeof(tpin_t));
  c->pin[args->pin].mode = PIN_UNCONFIGURED;
  c->pin[args->pin].inside = true;
  ESP_LOGI(TAG, "dev %u pin %u reset", c->id, args->pin);
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_IO, OP_LEVEL) {
  ERR_CHECK(args->level <= 1, ERR_BAD_ARG);
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  const tpin_t* p;
  ERR_TRY(pin_mode_is(c, args->pin, is_out, &p));
  return pin_set_level(c, args->pin, args->level);
}

OP_DECLARE_READ(CONTRACT_IO, OP_LEVEL) {
  ERR_TRY(sys_io_pins_check(&c->pins, c->id, args->pin));
  const tpin_t* p;
  ERR_TRY(pin_mode_is(c, args->pin, is_digital, &p));
  return CMD_REPLY(CONTRACT_IO, OP_LEVEL, .pin = args->pin, .level = p->level);
}

OP_DECLARE_WRITE(CONTRACT_IO, OP_TOGGLE) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  const tpin_t* p;
  ERR_TRY(pin_mode_is(c, args->pin, is_out, &p));
  return pin_set_level(c, args->pin, p->level == 0);
}

OP_DECLARE_WRITE(CONTRACT_IO, OP_PWM_DUTY) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  const tpin_t* p;
  ERR_TRY(pin_mode_is(c, args->pin, is_pwm, &p));
  ERR_CHECK_RANGE(args->duty, 0, IO_PWM_DUTY_MAX);
  c->pin[args->pin].duty = args->duty;
  ESP_LOGI(TAG, "dev %u pin %u duty %lu", c->id, args->pin, (unsigned long)args->duty);
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_IO, OP_PWM_FREQ) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  const tpin_t* p;
  ERR_TRY(pin_mode_is(c, args->pin, is_pwm, &p));
  ERR_CHECK_RANGE(args->frequency_Hz, 1, TESTDEV_PWM_FREQ_MAX);
  c->pin[args->pin].freq_Hz = args->frequency_Hz;
  ESP_LOGI(TAG, "dev %u pin %u frequency %lu Hz", c->id, args->pin, (unsigned long)args->frequency_Hz);
  return NULL;
}

// Edges need a digital pin, a window an ADC pin; the hysteresis and the event counter are not simulated.
OP_DECLARE_WRITE(CONTRACT_IO, OP_INTR) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  ERR_CHECK(args->cfg.mode <= SYS_IO_INTR_ADC_WINDOW_INSIDE && args->cfg.debounce <= 1, ERR_BAD_ARG);
  const tpin_t* p;
  if (args->cfg.mode >= SYS_IO_INTR_ADC_WINDOW_OUTSIDE) {
    ERR_TRY(pin_mode_is(c, args->pin, is_adc, &p));
  } else if (args->cfg.mode != SYS_IO_INTR_DISABLE) {
    ERR_TRY(pin_mode_is(c, args->pin, is_digital, &p));
  }
  tpin_t* pin = &c->pin[args->pin];
  pin->intr = args->cfg;
  pin->inside = in_window(pin);
  ESP_LOGI(TAG, "dev %u pin %u interrupt mode %u", c->id, args->pin, args->cfg.mode);
  return NULL;
}

OP_DECLARE_READ(CONTRACT_IO, OP_VOLTAGE) {
  ERR_TRY(sys_io_pins_check(&c->pins, c->id, args->pin));
  const tpin_t* p;
  ERR_TRY(pin_mode_is(c, args->pin, is_analog, &p));
  return CMD_REPLY(CONTRACT_IO, OP_VOLTAGE, .pin = args->pin, .voltage_mV = p->mV);
}

OP_DECLARE_WRITE(CONTRACT_IO, OP_VOLTAGE) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  const tpin_t* p;
  ERR_TRY(pin_mode_is(c, args->pin, is_dac, &p));
  ERR_CHECK_RANGE(args->voltage_mV, 0, TESTDEV_DAC_MAX_MV);
  c->pin[args->pin].mV = args->voltage_mV;
  ESP_LOGI(TAG, "dev %u pin %u output %ld mV", c->id, args->pin, (long)args->voltage_mV);
  return NULL;
}

SYS_IO_PINS_HANDLERS

/* ----------------------------------------------------------------------------
 * I2C: the simulated bus
 * ------------------------------------------------------------------------- */

static tchip_t* chip_at(testdev_t* c, uint8_t addr) {
  for (uint32_t i = 0; i < TESTDEV_I2C_CHIPS; i++) {
    if (c->chip[i].addr == addr && addr != 0) {
      return &c->chip[i];
    }
  }
  return NULL;
}

static err_h check_addr(const testdev_t* c, uint8_t addr) {
  ERR_CHECK(addr <= SYS_I2C_ADDR_MAX, ERR_I2C_BAD_DEV_CFG, .bus = c->id, .addr = addr);
  return NULL;
}

static err_h check_owner(const testdev_t* c, uint8_t addr) {
  ERR_TRY(check_addr(c, addr));
  ERR_CHECK(c->attached[addr], ERR_I2C_DEV_NOT_ADDED, .bus = c->id, .addr = addr);
  ERR_CHECK(c->owner[addr] == sys_device_caller(), ERR_I2C_NOT_OWNER, .bus = c->id, .addr = addr,
            .owner = c->owner[addr]);
  return NULL;
}

// One transfer to a chip: the write bytes (w1 then w2) are the register pointer and then data; a read comes from
// the pointer.
static err_h chip_xfer(testdev_t* c, uint8_t addr, const uint8_t* w1, size_t n1, const uint8_t* w2, size_t n2,
                       uint8_t* r, size_t rn) {
  const bool shape = (w1 != NULL || n1 == 0) && (w2 != NULL || n2 == 0) && (r != NULL || rn == 0) &&
                     (n1 > 0 || (n2 == 0 && rn > 0));
  ERR_CHECK(shape, ERR_BAD_ARG);
  ERR_CHECK(!c->stuck, ERR_I2C_BUS_STUCK, .bus = c->id, .addr = addr);
  tchip_t* chip = chip_at(c, addr);
  ERR_CHECK(chip != NULL, ERR_I2C_NO_ACK, .bus = c->id, .addr = addr);
  const size_t n = n1 + n2;
  for (size_t i = 0; i < n; i++) {
    const uint8_t b = i < n1 ? w1[i] : w2[i - n1];
    if (i == 0) {
      chip->ptr = b;
    } else {
      chip->regs[chip->ptr++] = b;
    }
  }
  for (size_t i = 0; i < rn; i++) {
    r[i] = chip->regs[chip->ptr++];
  }
  if (n > 1) {
    ESP_LOGD(TAG, "dev %u i2c 0x%02x write %u bytes from 0x%02x", c->id, addr, (unsigned)(n - 1), (unsigned)w1[0]);
  }
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_I2C, OP_ATTACH) {
  ERR_CHECK(args->addr <= SYS_I2C_ADDR_MAX && args->speed_Hz != 0, ERR_I2C_BAD_DEV_CFG, .bus = c->id,
            .addr = args->addr);
  ERR_CHECK(!c->attached[args->addr], ERR_I2C_ADDR_TAKEN, .bus = c->id, .addr = args->addr,
            .owner = c->owner[args->addr]);
  ERR_CHECK(!c->stuck, ERR_I2C_BUS_STUCK, .bus = c->id, .addr = args->addr);
  ERR_CHECK(chip_at(c, args->addr) != NULL, ERR_I2C_DEV_NOT_FOUND, .bus = c->id, .addr = args->addr);
  c->attached[args->addr] = true;
  c->owner[args->addr] = sys_device_caller();
  ESP_LOGI(TAG, "dev %u i2c 0x%02x attached by %u", c->id, args->addr, c->owner[args->addr]);
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_I2C, OP_DETACH) {
  ERR_TRY(check_owner(c, args->addr));
  c->attached[args->addr] = false;
  ESP_LOGI(TAG, "dev %u i2c 0x%02x detached", c->id, args->addr);
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_I2C, OP_XFER) {
  ERR_TRY(check_owner(c, args->addr));
  return chip_xfer(c, args->addr, args->w1, args->n1, args->w2, args->n2, args->r, args->rn);
}

OP_DECLARE_READ(CONTRACT_I2C, OP_PROBE) {
  ERR_TRY(check_addr(c, args->addr));
  ERR_CHECK(!c->stuck, ERR_I2C_BUS_STUCK, .bus = c->id, .addr = args->addr);
  return CMD_REPLY(CONTRACT_I2C, OP_PROBE, chip_at(c, args->addr) != NULL);
}

// NOLOCK: reads the chips without the lock, as a real scan reads the bus; FAKE_I2C in between shows in the next scan.
OP_DECLARE_READ(CONTRACT_I2C, OP_SCAN) {
  ERR_CHECK(!c->stuck, ERR_I2C_BUS_STUCK, .bus = c->id, .addr = 0);
  i2c_scan_t res = {{0}};
  for (uint8_t addr = SYS_I2C_SCAN_ADDR_FIRST; addr <= SYS_I2C_SCAN_ADDR_LAST; addr++) {
    if (chip_at(c, addr) != NULL) {
      res.found[addr / 32] |= 1u << (addr % 32);
    }
  }
  return cmd_reply(out, &res, sizeof(res));
}

OP_DECLARE_WRITE(CONTRACT_I2C, OP_RECOVER) {
  c->stuck = false;
  ESP_LOGI(TAG, "dev %u i2c bus recovered", c->id);
  return NULL;
}

// RAW takes the address for the one transfer: it fails on an address a driver owns.
static err_h raw_check(const testdev_t* c, uint8_t addr) {
  ERR_TRY(check_addr(c, addr));
  ERR_CHECK(!c->attached[addr], ERR_I2C_ADDR_TAKEN, .bus = c->id, .addr = addr, .owner = c->owner[addr]);
  return NULL;
}

OP_DECLARE_READ(CONTRACT_I2C, OP_RAW) {
  const i2c_raw_t head = {.addr = args->addr, .reg = args->reg};
  const uint32_t need = sizeof(head) + args->len;
  ERR_CHECK(args->len > 0, ERR_BAD_ARG);
  ERR_CHECK(need <= out->cap, ERR_CMD_OUT_FULL, .need = (uint16_t)need, .cap = (uint16_t)out->cap);
  ERR_TRY(raw_check(c, args->addr));
  ERR_TRY(chip_xfer(c, args->addr, &args->reg, 1, NULL, 0, out->data + sizeof(head), args->len));
  memcpy(out->data, &head, sizeof(head));
  out->len = need;
  return NULL;
}

OP_DECLARE_WRITE_TAIL(CONTRACT_I2C, OP_RAW) {
  ERR_CHECK(tail_len > 0, ERR_CMD_BAD_LEN, .got = (uint16_t)sizeof(*args), .need = (uint16_t)(sizeof(*args) + 1));
  ERR_TRY(raw_check(c, args->addr));
  ERR_TRY(chip_xfer(c, args->addr, &args->reg, 1, tail, tail_len, NULL, 0));
  ESP_LOGI(TAG, "dev %u i2c 0x%02x raw write %u bytes at 0x%02x", c->id, args->addr, (unsigned)tail_len, args->reg);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * PMON, HBRIDGE: channels
 * ------------------------------------------------------------------------- */

static err_h check_channel(const testdev_t* c, uint8_t channel) {
  ERR_CHECK(channel < c->cfg.channels, ERR_POWER_BAD_CHANNEL, .id = c->id, .channel = channel);
  return NULL;
}

// A channel's current changes: an armed alert it has reached is published once, and again after it fell below.
static err_h channel_set_mA(testdev_t* c, uint8_t channel, int32_t mA) {
  tch_t* ch = &c->ch[channel];
  ch->mA = mA;
  err_h err = NULL;
  for (uint32_t a = 0; a < 2; a++) {
    const bool over = ch->alert[a].armed && mA >= ch->alert[a].threshold_mA;
    if (over && !ch->alert[a].over) {
      ESP_LOGW(TAG, "dev %u channel %u current %ld mA over alert %lu", c->id, channel, (long)mA, (unsigned long)a);
      if (a == SYS_POWER_ALERT_OCP_WARNING) {
        ERR_KEEP_FIRST(err, SYS_EVENT_PUBLISH(PMON_EV_OCP_WARNING, c->id, channel, .mA = mA));
      } else {
        ERR_KEEP_FIRST(err, SYS_EVENT_PUBLISH(PMON_EV_OCP_CRITICAL, c->id, channel, .mA = mA));
      }
    }
    ch->alert[a].over = over;
  }
  return err;
}

OP_DECLARE_READ(CONTRACT_PMON, OP_VOLTAGE) {
  ERR_TRY(check_channel(c, args->channel));
  return CMD_REPLY(CONTRACT_PMON, OP_VOLTAGE, c->ch[args->channel].mV);
}

OP_DECLARE_READ(CONTRACT_PMON, OP_CURRENT) {
  ERR_TRY(check_channel(c, args->channel));
  return CMD_REPLY(CONTRACT_PMON, OP_CURRENT, c->ch[args->channel].mA);
}

OP_DECLARE_READ(CONTRACT_PMON, OP_POWER) {
  ERR_TRY(check_channel(c, args->channel));
  const int64_t mW = (int64_t)c->ch[args->channel].mV * c->ch[args->channel].mA / 1000;
  return CMD_REPLY(CONTRACT_PMON, OP_POWER, (int32_t)mW);
}

OP_DECLARE_WRITE(CONTRACT_PMON, OP_ALERT) {
  ERR_TRY(check_channel(c, args->channel));
  ERR_CHECK(args->alert <= SYS_POWER_ALERT_OCP_CRITICAL, ERR_BAD_ARG);
  tch_t* ch = &c->ch[args->channel];
  ch->alert[args->alert].armed = true;
  ch->alert[args->alert].threshold_mA = args->threshold_mA;
  ch->alert[args->alert].over = false;
  ESP_LOGI(TAG, "dev %u channel %u alert %u at %ld mA", c->id, args->channel, args->alert, (long)args->threshold_mA);
  return channel_set_mA(c, args->channel, ch->mA);
}

static err_h check_drive_channel(const testdev_t* c, uint8_t channel) {
  ERR_CHECK(channel < c->cfg.channels, ERR_DEV_BAD_PIN, .id = c->id, .pin = channel);
  return NULL;
}

enum { BRIDGE_DRIVE = 0, BRIDGE_BRAKE = 1, BRIDGE_COAST = 2 };

OP_DECLARE_WRITE(CONTRACT_HBRIDGE, OP_DRIVE) {
  ERR_TRY(check_drive_channel(c, args->channel));
  ERR_CHECK_RANGE(args->drive, -SYS_HBRIDGE_DRIVE_MAX, SYS_HBRIDGE_DRIVE_MAX);
  tch_t* ch = &c->ch[args->channel];
  ch->drive = args->drive;
  ch->bridge = BRIDGE_DRIVE;
  ESP_LOGI(TAG, "dev %u bridge %u drive %d", c->id, args->channel, args->drive);
  const int32_t steps = args->drive < 0 ? -args->drive : args->drive;
  return channel_set_mA(c, args->channel, steps * 1000 / SYS_HBRIDGE_DRIVE_MAX);
}

OP_DECLARE_WRITE(CONTRACT_HBRIDGE, OP_BRAKE) {
  ERR_TRY(check_drive_channel(c, args->channel));
  c->ch[args->channel].drive = 0;
  c->ch[args->channel].bridge = BRIDGE_BRAKE;
  ESP_LOGI(TAG, "dev %u bridge %u brake", c->id, args->channel);
  return channel_set_mA(c, args->channel, 0);
}

OP_DECLARE_WRITE(CONTRACT_HBRIDGE, OP_COAST) {
  ERR_TRY(check_drive_channel(c, args->channel));
  c->ch[args->channel].drive = 0;
  c->ch[args->channel].bridge = BRIDGE_COAST;
  ESP_LOGI(TAG, "dev %u bridge %u coast", c->id, args->channel);
  return channel_set_mA(c, args->channel, 0);
}

OP_DECLARE_READ(CONTRACT_HBRIDGE, OP_CURRENT) {
  ERR_TRY(check_drive_channel(c, args->channel));
  return CMD_REPLY(CONTRACT_HBRIDGE, OP_CURRENT, c->ch[args->channel].mA);
}

/* ----------------------------------------------------------------------------
 * VREG, PD, SUPPLY
 * ------------------------------------------------------------------------- */

OP_DECLARE_WRITE(CONTRACT_VREG, OP_ENABLE) {
  ERR_CHECK(args->enable <= 1, ERR_BAD_ARG);
  const bool on = args->enable != 0 && !c->vreg.enable;
  c->vreg.enable = args->enable;
  ESP_LOGI(TAG, "dev %u regulator %s", c->id, args->enable ? "on" : "off");
  return on ? SYS_EVENT_PUBLISH(VREG_EV_READY, c->id, 0, .mV = (int32_t)c->vreg.voltage_mV) : NULL;
}

OP_DECLARE_WRITE(CONTRACT_VREG, OP_VOLTAGE) {
  ERR_CHECK_RANGE(args->voltage_mV, TESTDEV_VREG_MIN_MV, TESTDEV_VREG_MAX_MV);
  const bool higher = c->vreg.enable && args->voltage_mV > c->vreg.voltage_mV;
  c->vreg.voltage_mV = args->voltage_mV;
  ESP_LOGI(TAG, "dev %u regulator voltage %lu mV", c->id, (unsigned long)args->voltage_mV);
  return higher ? SYS_EVENT_PUBLISH(VREG_EV_READY, c->id, 0, .mV = (int32_t)args->voltage_mV) : NULL;
}

OP_DECLARE_WRITE(CONTRACT_VREG, OP_CURRENT) {
  ERR_CHECK_RANGE(args->current_mA, 0, TESTDEV_VREG_MAX_MA);
  c->vreg.current_mA = args->current_mA;
  ESP_LOGI(TAG, "dev %u regulator current limit %lu mA", c->id, (unsigned long)args->current_mA);
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_PD, OP_SETTINGS) {
  ERR_CHECK_RANGE(args->voltage_mV, TESTDEV_PD_MIN_MV, TESTDEV_PD_MAX_MV);
  ERR_CHECK_RANGE(args->current_mA, 0, TESTDEV_PD_MAX_MA);
  c->pd = *args;
  ESP_LOGI(TAG, "dev %u pd asks %lu mV %lu mA", c->id, (unsigned long)args->voltage_mV,
           (unsigned long)args->current_mA);
  return NULL;
}

OP_DECLARE_READ(CONTRACT_PD, OP_SETTINGS) {
  return CMD_REPLY(CONTRACT_PD, OP_SETTINGS, .voltage_mV = c->pd.voltage_mV, .current_mA = c->pd.current_mA);
}

OP_DECLARE_READ(CONTRACT_PD, OP_OPTIONS) {
  return cmd_reply(out, k_pd_options, sizeof(k_pd_options));
}

// The highest voltage the charger offers, with the current of that offer.
OP_DECLARE_READ(CONTRACT_PD, OP_LIMITS) {
  const sys_power_usb_pd_option_t* top = &k_pd_options[0];
  for (uint32_t i = 1; i < ARRAY_LEN(k_pd_options); i++) {
    if (k_pd_options[i].max_mV > top->max_mV) {
      top = &k_pd_options[i];
    }
  }
  return CMD_REPLY(CONTRACT_PD, OP_LIMITS, .voltage_mV = top->max_mV, .current_mA = top->max_mA);
}

OP_DECLARE_WRITE(CONTRACT_SUPPLY, OP_SOURCE) {
  ERR_CHECK(args->source <= SYS_POWER_SUPPLY_B, ERR_BAD_ARG);
  c->source = args->source;
  ESP_LOGI(TAG, "dev %u supply source %u", c->id, args->source);
  return NULL;
}

OP_DECLARE_READ(CONTRACT_SUPPLY, OP_SOURCE) {
  return CMD_REPLY(CONTRACT_SUPPLY, OP_SOURCE, c->source);
}

/* ----------------------------------------------------------------------------
 * UART: a port with an RX queue
 * ------------------------------------------------------------------------- */

static bool has_reader(const testdev_t* c) {
  return c->reader != SYS_DEVICE_ID_NONE || c->hook != NULL;
}

// A message arrives: to the subscriber, to the hook, else into the queue (the oldest goes when it is full).
static err_h rx_in(testdev_t* c, const uint8_t* data, uint32_t len) {
  ESP_LOGI(TAG, "dev %u uart rx %u bytes", c->id, (unsigned)len);
  if (c->reader != SYS_DEVICE_ID_NONE) {
    struct __attribute__((packed)) {
      sys_uart_rx_t hdr;
      uint8_t data[TESTDEV_MSG_MAX];
    } msg = {.hdr = {.port = c->id}};
    memcpy(msg.data, data, len);
    return sys_device_deliver(c->reader, c->reader_gen, c->reader_op, &msg, (uint32_t)sizeof(msg.hdr) + len);
  }
  if (c->hook != NULL) {
    c->hook(c->hook_ctx, data, len);
    return NULL;
  }
  portENTER_CRITICAL(&c->rx_lock);
  if (c->rx_count == TESTDEV_UART_QUEUE) {
    c->rx_head = (uint8_t)((c->rx_head + 1u) % TESTDEV_UART_QUEUE);
    c->rx_count--;
  }
  tmsg_t* m = &c->rx[(c->rx_head + c->rx_count) % TESTDEV_UART_QUEUE];
  m->len = (uint8_t)len;
  memcpy(m->data, data, len);
  c->rx_count++;
  portEXIT_CRITICAL(&c->rx_lock);
  return NULL;
}

static void rx_flush(testdev_t* c) {
  portENTER_CRITICAL(&c->rx_lock);
  c->rx_head = c->rx_count = 0;
  portEXIT_CRITICAL(&c->rx_lock);
}

// The bytes leave the port: logged, and they come back as a message while LOOPBACK is on.
static err_h tx_out(testdev_t* c, const uint8_t* data, uint32_t len, uint16_t wait_ms) {
  ERR_CHECK(len > 0 && len <= TESTDEV_MSG_MAX && wait_ms <= SYS_UART_WAIT_MAX_MS, ERR_BAD_ARG);
  ESP_LOGI(TAG, "dev %u uart tx %u bytes", c->id, (unsigned)len);
  ESP_LOG_BUFFER_HEX(TAG, data, len);
  return c->loopback != 0 ? rx_in(c, data, len) : NULL;
}

static err_h rx_pop(testdev_t* c, cmd_out_t* out) {
  tmsg_t m;
  portENTER_CRITICAL(&c->rx_lock);
  const bool any = c->rx_count != 0;
  if (any) {
    m = c->rx[c->rx_head];
    c->rx_head = (uint8_t)((c->rx_head + 1u) % TESTDEV_UART_QUEUE);
    c->rx_count--;
  }
  portEXIT_CRITICAL(&c->rx_lock);
  return any ? cmd_reply(out, m.data, m.len) : NULL;  // none came: an empty answer
}

OP_DECLARE_READ(CONTRACT_UART, OP_INFO) {
  const sys_uart_info_t info = {
      .mode = c->cfg.uart_mode, .baud = c->baud, .parity = SYS_UART_PARITY_NONE, .stop_bits = 1};
  return cmd_reply(out, &info, sizeof(info));
}

OP_DECLARE_READ(CONTRACT_UART, OP_BAUD) {
  return CMD_REPLY(CONTRACT_UART, OP_BAUD, c->baud);
}

OP_DECLARE_WRITE(CONTRACT_UART, OP_BAUD) {
  ERR_CHECK(*args >= SYS_UART_BAUD_MIN && *args <= SYS_UART_BAUD_MAX, ERR_UART_BAD_BAUD, .baud = *args);
  c->baud = *args;
  ESP_LOGI(TAG, "dev %u uart baud %lu", c->id, (unsigned long)*args);
  return NULL;
}

// Refused on a port a subscriber holds (but for it), and on a FRAMED one a hook holds: raw bytes would break frames.
OP_DECLARE_WRITE_TAIL(CONTRACT_UART, OP_SEND) {
  const bool held = c->reader != SYS_DEVICE_ID_NONE ? c->reader != sys_device_caller()
                                                    : c->hook != NULL && c->cfg.uart_mode == SYS_UART_MODE_FRAMED;
  ERR_CHECK(!held, ERR_UART_IN_USE, .port = c->id, .owner = c->reader);
  return tx_out(c, tail, tail_len, args->wait_ms);
}

OP_DECLARE_WRITE(CONTRACT_UART, OP_SEND_FRAME) {
  return tx_out(c, args->frame.data, args->frame.len, args->wait_ms);
}

OP_DECLARE_READ(CONTRACT_UART, OP_RECEIVE) {
  ERR_CHECK(args->wait_ms <= SYS_UART_WAIT_MAX_MS, ERR_BAD_ARG);
  ERR_CHECK(!has_reader(c), ERR_UART_IN_USE, .port = c->id, .owner = c->reader);
  return rx_pop(c, out);
}

// Flushes the queue, sends, and answers the message that comes back (with LOOPBACK on, the bytes themselves).
OP_DECLARE_WRITE_TAIL(CONTRACT_UART, OP_TRANSACT) {
  ERR_CHECK(tail_len > 0 && args->wait_ms <= SYS_UART_WAIT_MAX_MS, ERR_BAD_ARG);
  ERR_CHECK(!has_reader(c), ERR_UART_IN_USE, .port = c->id, .owner = c->reader);
  rx_flush(c);
  ERR_TRY(tx_out(c, tail, tail_len, args->wait_ms));
  return rx_pop(c, out);
}

OP_DECLARE_WRITE(CONTRACT_UART, OP_FLUSH) {
  ERR_CHECK(!has_reader(c), ERR_UART_IN_USE, .port = c->id, .owner = c->reader);
  rx_flush(c);
  return NULL;
}

// The caller (a device, in its create()) the reader: each message to its own op. `op` 0: it lets go.
OP_DECLARE_WRITE(CONTRACT_UART, OP_SUBSCRIBE) {
  const uint8_t caller = sys_device_caller();
  ERR_CHECK(caller != SYS_DEVICE_ID_NONE, ERR_BAD_ARG);
  const bool busy = args->op != 0 && has_reader(c);
  if (args->op == 0 && c->reader == caller) {
    c->reader = SYS_DEVICE_ID_NONE;
  } else if (args->op != 0 && !busy) {
    c->reader = caller;
    c->reader_gen = sys_device_gen(caller);
    c->reader_op = args->op;
    rx_flush(c);
  }
  ERR_CHECK(!busy, ERR_UART_IN_USE, .port = c->id, .owner = c->reader);
  return NULL;
}

// A C reader (a provider adapter). `cb` NULL: the hook with this `ctx` lets go.
OP_DECLARE_WRITE(CONTRACT_UART, OP_RX_HOOK) {
  const bool busy = args->cb != NULL && has_reader(c);
  if (args->cb == NULL && c->hook_ctx == args->ctx) {
    c->hook = NULL;
  } else if (args->cb != NULL && !busy) {
    c->hook = args->cb;
    c->hook_ctx = args->ctx;
    rx_flush(c);
  }
  ERR_CHECK(!busy, ERR_UART_IN_USE, .port = c->id, .owner = c->reader);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * EVENT: a device that hears events
 * ------------------------------------------------------------------------- */

OP_DECLARE_ON_EVENT {
  testdev_heard_t* h = &c->heard;
  h->count++;
  h->key = event->key;
  h->len = event->len;
  memset(h->data, 0, sizeof(h->data));
  memcpy(h->data, event->data, event->len < sizeof(h->data) ? event->len : sizeof(h->data));
  ESP_LOGI(TAG, "dev %u heard event 0x%08lx from %u channel %u", c->id, (unsigned long)event->key.tag,
           (unsigned)event->key.source, (unsigned)event->key.channel);
  return NULL;
}

static err_h unlisten(void* arg) {
  testdev_t* c = arg;
  if (c->sub == 0) {
    return NULL;
  }
  const uint16_t sub = c->sub;
  c->sub = 0;
  return sys_event_unsubscribe(sub);
}

/* ----------------------------------------------------------------------------
 * Own operations
 * ------------------------------------------------------------------------- */

OP_DECLARE_READ(OWN_TESTDEV, OP_FAKE_PIN) {
  ERR_TRY(sys_io_pins_check(&c->pins, c->id, args->pin));
  return CMD_REPLY(OWN_TESTDEV, OP_FAKE_PIN, .pin = args->pin, .level = c->pin[args->pin].level,
                   .voltage_mV = c->pin[args->pin].mV);
}

// The outside changes: a level an input reads (or an output is shorted to), a voltage an ADC pin reads.
OP_DECLARE_WRITE(OWN_TESTDEV, OP_FAKE_PIN) {
  ERR_TRY(sys_io_pins_check(&c->pins, c->id, args->pin));
  ERR_CHECK(args->level <= 1, ERR_BAD_ARG);
  ERR_TRY(pin_set_level(c, args->pin, args->level));
  return pin_set_mV(c, args->pin, args->voltage_mV);
}

OP_DECLARE_WRITE(OWN_TESTDEV, OP_FAKE_I2C) {
  ERR_TRY(check_addr(c, args->addr));
  ERR_CHECK(args->addr >= SYS_I2C_SCAN_ADDR_FIRST && args->addr <= SYS_I2C_SCAN_ADDR_LAST && args->present <= 1,
            ERR_I2C_BAD_DEV_CFG, .bus = c->id, .addr = args->addr);
  tchip_t* chip = chip_at(c, args->addr);
  if (args->present == 0 && chip != NULL) {
    chip->addr = 0;
  } else if (args->present != 0 && chip == NULL) {
    for (uint32_t i = 0; i < TESTDEV_I2C_CHIPS && chip == NULL; i++) {  // addr 0 is a free slot
      chip = c->chip[i].addr == 0 ? &c->chip[i] : NULL;
    }
    ERR_CHECK(chip != NULL, ERR_BAD_ARG);
    memset(chip, 0, sizeof(*chip));
    chip->addr = args->addr;
  }
  ESP_LOGI(TAG, "dev %u i2c 0x%02x %s", c->id, args->addr, args->present ? "present" : "gone");
  return NULL;
}

OP_DECLARE_READ(OWN_TESTDEV, OP_FAKE_CHANNEL) {
  ERR_TRY(check_channel(c, args->channel));
  return CMD_REPLY(OWN_TESTDEV, OP_FAKE_CHANNEL, .channel = args->channel, .voltage_mV = c->ch[args->channel].mV,
                   .current_mA = c->ch[args->channel].mA);
}

OP_DECLARE_WRITE(OWN_TESTDEV, OP_FAKE_CHANNEL) {
  ERR_TRY(check_channel(c, args->channel));
  c->ch[args->channel].mV = args->voltage_mV;
  ESP_LOGI(TAG, "dev %u channel %u measures %ld mV %ld mA", c->id, args->channel, (long)args->voltage_mV,
           (long)args->current_mA);
  return channel_set_mA(c, args->channel, args->current_mA);
}

// An event a real driver publishes, as if the hardware had raised it (the state does not change).
OP_DECLARE_WRITE(OWN_TESTDEV, OP_FAKE_FAULT) {
  const uint8_t ch = args->channel;
  const int32_t v = args->value;
  ESP_LOGW(TAG, "dev %u fault %u channel %u value %ld", c->id, args->kind, ch, (long)v);
  switch (args->kind) {
    case TESTDEV_FAULT_IO_RISING:
      ERR_TRY(sys_io_pins_check(&c->pins, c->id, ch));
      return SYS_EVENT_PUBLISH(IO_EV_RISING, c->id, ch);
    case TESTDEV_FAULT_IO_FALLING:
      ERR_TRY(sys_io_pins_check(&c->pins, c->id, ch));
      return SYS_EVENT_PUBLISH(IO_EV_FALLING, c->id, ch);
    case TESTDEV_FAULT_PMON_WARNING:
      ERR_TRY(check_channel(c, ch));
      return SYS_EVENT_PUBLISH(PMON_EV_OCP_WARNING, c->id, ch, .mA = v);
    case TESTDEV_FAULT_PMON_CRITICAL:
      ERR_TRY(check_channel(c, ch));
      return SYS_EVENT_PUBLISH(PMON_EV_OCP_CRITICAL, c->id, ch, .mA = v);
    case TESTDEV_FAULT_VREG_OVP:
      return SYS_EVENT_PUBLISH(VREG_EV_OVP, c->id, 0, .mV = v);
    case TESTDEV_FAULT_VREG_OCP:
      return SYS_EVENT_PUBLISH(VREG_EV_OCP, c->id, 0, .mA = v);
    case TESTDEV_FAULT_VREG_SCP:
      return SYS_EVENT_PUBLISH(VREG_EV_SCP, c->id, 0);
    case TESTDEV_FAULT_HB_OVERCURRENT:
      ERR_TRY(check_drive_channel(c, ch));
      return SYS_EVENT_PUBLISH(HBRIDGE_EV_OVERCURRENT, c->id, ch, .mA = v);
    case TESTDEV_FAULT_HB_THERMAL:
      ERR_TRY(check_drive_channel(c, ch));
      return SYS_EVENT_PUBLISH(HBRIDGE_EV_THERMAL_OR_UVLO, c->id, ch, .mA = v);
    case TESTDEV_FAULT_HB_EXTERNAL:
      ERR_TRY(check_drive_channel(c, ch));
      return SYS_EVENT_PUBLISH(HBRIDGE_EV_EXTERNAL, c->id, ch, .mA = v);
    case TESTDEV_FAULT_UART_OVERFLOW:
      return SYS_EVENT_PUBLISH(UART_EV_OVERFLOW, c->id, 0, .dropped = (uint16_t)v);
    case TESTDEV_FAULT_I2C_STUCK:
      c->stuck = true;
      return NULL;
    default:
      return ERR_NEW(ERR_BAD_ARG);
  }
}

// A message arrives on the UART port, `copies` times.
OP_DECLARE_WRITE_TAIL(OWN_TESTDEV, OP_FAKE_UART_RX) {
  ERR_CHECK(tail_len > 0 && tail_len <= TESTDEV_MSG_MAX && args->copies >= 1 && args->copies <= TESTDEV_UART_QUEUE,
            ERR_BAD_ARG);
  err_h err = NULL;
  for (uint8_t i = 0; i < args->copies; i++) {
    ERR_KEEP_FIRST(err, rx_in(c, tail, tail_len));
  }
  return err;
}

// ON_EVENT hears the events that pass the filter (one subscription: a new LISTEN replaces it).
OP_DECLARE_WRITE(OWN_TESTDEV, OP_LISTEN) {
  ERR_TRY(unlisten(c));
  memset(&c->heard, 0, sizeof(c->heard));
  const sys_event_key_t filter = {.tag = args->tag, .source = args->source, .channel = args->channel};
  ERR_TRY(sys_device_event_subscribe(filter, c->id, &c->sub));
  ESP_LOGI(TAG, "dev %u listens to 0x%08lx from %u channel %u", c->id, (unsigned long)args->tag, (unsigned)args->source,
           (unsigned)args->channel);
  return NULL;
}

OP_DECLARE_READ(OWN_TESTDEV, OP_HEARD) {
  return cmd_reply(out, &c->heard, sizeof(c->heard));
}

OP_DECLARE_READ(OWN_TESTDEV, OP_VREG) {
  return cmd_reply(out, &c->vreg, sizeof(c->vreg));
}

OP_DECLARE_READ(OWN_TESTDEV, OP_LOOPBACK) {
  return CMD_REPLY(OWN_TESTDEV, OP_LOOPBACK, .loopback = c->loopback);
}

OP_DECLARE_WRITE(OWN_TESTDEV, OP_LOOPBACK) {
  ERR_CHECK(args->loopback <= 1, ERR_BAD_ARG);
  c->loopback = args->loopback;
  ESP_LOGI(TAG, "dev %u uart loopback %u", c->id, args->loopback);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  ERR_TRY(unlisten(c));
  reset_state(c);  // the pins' locks and the attached addresses are other devices', they stay
  ESP_LOGI(TAG, "dev %u reset", c->id);
  return NULL;
}

// The safe state of a real device: outputs low, PWM and DAC at 0, bridges coasting, regulator and supply off.
static err_h suspend(testdev_t* c) {
  for (uint8_t i = 0; i < c->cfg.pins; i++) {
    tpin_t* p = &c->pin[i];
    if (is_out(p->mode)) {
      p->level = 0;
    }
    p->duty = is_pwm(p->mode) ? 0 : p->duty;
    p->mV = is_dac(p->mode) ? 0 : p->mV;
  }
  for (uint32_t i = 0; i < TESTDEV_CHANNELS_MAX; i++) {
    c->ch[i].drive = 0;
    c->ch[i].bridge = BRIDGE_COAST;
  }
  c->vreg.enable = 0;
  c->source = SYS_POWER_SUPPLY_OFF;
  ESP_LOGW(TAG, "dev %u suspended: outputs off", c->id);
  return NULL;
}

static err_h resume(testdev_t* c) {
  ESP_LOGI(TAG, "dev %u resumed", c->id);
  return NULL;
}

static err_h create(uint8_t id, testdev_t* c, const testdev_cfg_t* cfg) {
  const bool ok = cfg->pins >= 1 && cfg->pins <= SYS_IO_PINS_MAX && cfg->channels >= 1 &&
                  cfg->channels <= TESTDEV_CHANNELS_MAX && cfg->uart_mode <= SYS_UART_MODE_RAW_LINE &&
                  cfg->uart_baud >= SYS_UART_BAUD_MIN && cfg->uart_baud <= SYS_UART_BAUD_MAX;
  ERR_CHECK(ok, ERR_BAD_ARG);
  c->id = id;
  c->cfg = *cfg;
  c->reader = SYS_DEVICE_ID_NONE;
  portMUX_INITIALIZE(&c->rx_lock);
  sys_io_pins_init(&c->pins, cfg->pins);
  reset_state(c);
  ERR_TRY(sys_device_res_add(id, &c->sub_res, unlisten, c));
  ESP_LOGI(TAG, "dev %u created: %u pins, %u channels", id, cfg->pins, cfg->channels);
  return NULL;
}

/*******************************************************************************
 * TESTDEV : type registration (k_ops & SYS_DEVICE_TYPE_DEFINE)
 ******************************************************************************/

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),
    // IO
    OP_ENTRY(CONTRACT_IO, OP_LEVEL), OP_ENTRY_W(CONTRACT_IO, OP_TOGGLE), OP_ENTRY_W(CONTRACT_IO, OP_PWM_DUTY),
    OP_ENTRY_W(CONTRACT_IO, OP_PWM_FREQ), OP_ENTRY_W(CONTRACT_IO, OP_MODE), OP_ENTRY_W(CONTRACT_IO, OP_RESET),
    OP_ENTRY_W(CONTRACT_IO, OP_INTR), OP_ENTRY(CONTRACT_IO, OP_VOLTAGE), SYS_IO_PINS_ENTRIES,
    // I2C
    OP_ENTRY_W(CONTRACT_I2C, OP_ATTACH), OP_ENTRY_W(CONTRACT_I2C, OP_DETACH), OP_ENTRY_W(CONTRACT_I2C, OP_XFER),
    OP_ENTRY_R(CONTRACT_I2C, OP_PROBE), OP_ENTRY_R(CONTRACT_I2C, OP_SCAN), OP_ENTRY_W(CONTRACT_I2C, OP_RECOVER),
    OP_ENTRY(CONTRACT_I2C, OP_RAW),
    // EVENT
    OP_ENTRY_W(CONTRACT_EVENT, OP_ON_EVENT),
    // PMON, VREG, PD, SUPPLY, HBRIDGE
    OP_ENTRY_R(CONTRACT_PMON, OP_VOLTAGE), OP_ENTRY_R(CONTRACT_PMON, OP_CURRENT), OP_ENTRY_W(CONTRACT_PMON, OP_ALERT),
    OP_ENTRY_R(CONTRACT_PMON, OP_POWER),
    OP_ENTRY_W(CONTRACT_VREG, OP_ENABLE), OP_ENTRY_W(CONTRACT_VREG, OP_VOLTAGE), OP_ENTRY_W(CONTRACT_VREG, OP_CURRENT),
    OP_ENTRY(CONTRACT_PD, OP_SETTINGS), OP_ENTRY_R(CONTRACT_PD, OP_OPTIONS), OP_ENTRY_R(CONTRACT_PD, OP_LIMITS),
    OP_ENTRY(CONTRACT_SUPPLY, OP_SOURCE),
    OP_ENTRY_W(CONTRACT_HBRIDGE, OP_DRIVE), OP_ENTRY_W(CONTRACT_HBRIDGE, OP_BRAKE),
    OP_ENTRY_W(CONTRACT_HBRIDGE, OP_COAST), OP_ENTRY_R(CONTRACT_HBRIDGE, OP_CURRENT),
    // UART
    OP_ENTRY_R(CONTRACT_UART, OP_INFO), OP_ENTRY(CONTRACT_UART, OP_BAUD), OP_ENTRY_W(CONTRACT_UART, OP_SEND),
    OP_ENTRY_R(CONTRACT_UART, OP_RECEIVE), OP_ENTRY_W(CONTRACT_UART, OP_FLUSH), OP_ENTRY_W(CONTRACT_UART, OP_SUBSCRIBE),
    OP_ENTRY_W(CONTRACT_UART, OP_TRANSACT), OP_ENTRY_W(CONTRACT_UART, OP_SEND_FRAME),
    OP_ENTRY_W(CONTRACT_UART, OP_RX_HOOK),
    // Own
    OP_ENTRY(OWN_TESTDEV, OP_FAKE_PIN), OP_ENTRY_W(OWN_TESTDEV, OP_FAKE_I2C), OP_ENTRY(OWN_TESTDEV, OP_FAKE_CHANNEL),
    OP_ENTRY_W(OWN_TESTDEV, OP_FAKE_FAULT), OP_ENTRY_W(OWN_TESTDEV, OP_FAKE_UART_RX),
    OP_ENTRY_W(OWN_TESTDEV, OP_LISTEN),
    OP_ENTRY_R(OWN_TESTDEV, OP_HEARD), OP_ENTRY_R(OWN_TESTDEV, OP_VREG), OP_ENTRY(OWN_TESTDEV, OP_LOOPBACK),
};

SYS_DEVICE_TYPE_DEFINE(TESTDEV, testdev_cfg_t, create, suspend, resume, k_ops)
