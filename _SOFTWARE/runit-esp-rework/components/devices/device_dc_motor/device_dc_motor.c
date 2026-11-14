/*
 * DC motor: a brushed DC motor on one channel of a motor driver (any device with the HBRIDGE contract: the DRV8962),
 * type 0x004B. The whole device is this file: nothing includes it, the rest of the firmware reaches it by its ID, its
 * own ops (0xFFxx) and its own events (module SYS_EVENT_MODULE_TYPE(DC_MOTOR_TYPE)).
 *
 * The motor is what a program drives: a speed in percent that it reaches along a ramp, never in one jump; through zero
 * when the direction changes (a pause there spares the gearbox); with a dead zone round zero, a speed where the motor
 * starts turning, a ceiling (a 6 V motor on 12 V: 50 %), a trim that matches it to its pair and a reversed direction.
 * The bridge stays the raw output: this device is the motor's behaviour.
 *
 * Speed is in tenths of a percent on the wire: -1000 (full reverse) .. 1000 (full forward); 0..1000 for a motor on a
 * half bridge (`one_way`). The output (where the ramp is) maps onto the drive: 0 stops (brake or coast, `stop_mode`),
 * anything else is min_pct + |speed| x (max_pct - min_pct), scaled by the trim, never over max_pct.
 */
#include "board_time.h"
#include "sys_device_driver.h"
#include "sys_device_timer.h"
#include "sys_event.h"
#include "sys_hbridge.h"

SYS_DEVICE_TYPE_DECLARE(DC_MOTOR, 0x004B)  // DC_MOTOR_TYPE: also the module of the own events

/*******************************************************************************
 * DC_MOTOR : own events (SYS_EVENT_MODULE_TYPE(DC_MOTOR_TYPE), source = motor, channel 0)
 * Published deferred under the motor's lock:
 *   STALL   - stall stopped the motor; payload = bridge current mA (NOTIFY)
 *   REACHED - target speed reached; payload = target speed_x10 (code flow)
 ******************************************************************************/
#define DC_MOTOR_EVENTS(X, P)                                   \
  X(P, STALL,   0x01, SYS_EVENT_NOTIFY, { int32_t mA; })        \
  X(P, REACHED, 0x02, 0,                { int16_t speed_x10; })

SYS_DEVICE_OWN_EVENTS(DC_MOTOR)

/*******************************************************************************
 * OWN_DC_MOTOR : own operations (0xFFxx)
 *   OP_SPEED (RW), OP_OUTPUT (R), OP_STOP (W), OP_BRAKE (W), OP_COAST (W),
 *   OP_SETTINGS (RW), OP_CURRENT (R), OP_STALLED (R), OP_TICK (W, LOCAL)
 ******************************************************************************/

/** @brief SPEED's value, both sides. */
typedef struct __attribute__((packed)) {
  int16_t speed_x10;  /**< Tenths of a percent: -1000 (full reverse) .. 1000 (full forward) */
} dc_motor_speed_t;

/** @brief The settings that change live: CREATE takes them after the bridge and the channel, SETTINGS reads and
 * writes them with the same names. */
typedef struct __attribute__((packed)) {
  uint8_t invert;             /**< 1 = forward and reverse swapped (instead of swapping the wires) */
  uint8_t stop_mode;          /**< What 0 does: dc_motor_stop_e */
  uint8_t max_pct;            /**< Highest drive, 1..100 % (a 6 V motor on 12 V: 50) */
  uint8_t min_pct;            /**< Drive where the motor starts turning, 0..60 %: any speed past the dead zone starts
                                   here */
  uint8_t deadzone_pct;       /**< A speed under it is 0, 0..20 % (joystick or slider noise) */
  int8_t trim_pct;            /**< Drive scaled by 100 + trim percent, -20..20 (match the motor to its pair) */
  uint16_t ramp_up_ms;        /**< 0 to full speed, 0..10000 ms (0 = at once) */
  uint16_t ramp_down_ms;      /**< Full speed to 0, 0..10000 ms (0 = at once) */
  uint16_t reverse_pause_ms;  /**< Stopped this long between the directions, 0..2000 ms */
  uint16_t stall_mA;          /**< A current at or over it for stall_ms is a stall: the motor stops (0 = not watched) */
  uint16_t stall_ms;          /**< How long the stall current lasts before the motor stops, 50..5000 ms */
} dc_motor_settings_t;

#define OWN_DC_MOTOR_OPERATIONS(X, P)                                    \
  X(P, OP_SPEED,    0x00, cmd_none_t, dc_motor_speed_t,    0)            \
  X(P, OP_OUTPUT,   0x01, cmd_none_t, int32_t,             0)            \
  X(P, OP_STOP,     0x02, cmd_none_t, cmd_none_t,          0)            \
  X(P, OP_BRAKE,    0x03, cmd_none_t, cmd_none_t,          0)            \
  X(P, OP_COAST,    0x04, cmd_none_t, cmd_none_t,          0)            \
  X(P, OP_SETTINGS, 0x05, cmd_none_t, dc_motor_settings_t, 0)            \
  X(P, OP_CURRENT,  0x06, cmd_none_t, int32_t,             0)            \
  X(P, OP_STALLED,  0x07, cmd_none_t, uint8_t,             0)            \
  X(P, OP_TICK,     0x08, cmd_none_t, cmd_none_t,          SYS_DEVICE_OP_LOCAL)

SYS_DEVICE_OWN_DECLARE(OWN_DC_MOTOR)

/*******************************************************************************
 * DC_MOTOR : limits & config
 ******************************************************************************/
typedef enum {
  DC_MOTOR_STOP_BRAKE = 0,  // shorts the motor: it stops fast and holds
  DC_MOTOR_STOP_COAST = 1,  // releases the motor: it rolls to a stop
} dc_motor_stop_e;

#define DC_MOTOR_SPEED_MAX        1000  // tenths of a percent: 100 %
#define DC_MOTOR_MIN_PCT_MAX      60
#define DC_MOTOR_DEADZONE_PCT_MAX 20
#define DC_MOTOR_TRIM_PCT_MAX     20
#define DC_MOTOR_RAMP_MS_MAX      10000
#define DC_MOTOR_PAUSE_MS_MAX     2000
#define DC_MOTOR_STALL_MS_MIN     50
#define DC_MOTOR_STALL_MS_MAX     5000

/** @brief CREATE's struct (wire format). */
typedef struct __attribute__((packed)) {
  uint8_t bridge;                /**< The motor driver's device ID (a device with the HBRIDGE contract) */
  uint8_t channel;               /**< Its channel the motor is on */
  uint8_t one_way;               /**< 1 = a half bridge: forward only */
  dc_motor_settings_t settings;  /**< The settings that change live */
} dc_motor_cfg_t;

/* ----------------------------------------------------------------------------
 * The driver
 * ------------------------------------------------------------------------- */

#define TICK_MS       10u     // the timer's period: a step of the ramp at least this often while it moves
#define STALL_POLL_MS 50u     // the current is read this often while the motor turns
#define FULL          100000  // the ramp's unit: 100 % (fine enough that a 10 s ramp still moves every tick)
#define X10_TO_FULL   (FULL / DC_MOTOR_SPEED_MAX)

// What the bridge channel was last told: a drive, a brake, a coast (nothing yet: not driven).
typedef enum { SENT_NONE = 0, SENT_DRIVE, SENT_BRAKE, SENT_COAST } sent_e;

typedef struct {
  uint8_t id;
  dc_motor_cfg_t cfg;           // the live settings are cfg.settings
  dc_motor_settings_t initial;  // the settings create took: RESET returns to them
  sys_device_timer_t timer;     // the ramp's step (TICK): runs while the motor moves or is watched
  bool ticking;                 // the timer runs: the next step counts the time since stepped_ms
  uint64_t stepped_ms;          // uptime of the last step
  int16_t target_x10;           // the speed as written (SPEED's read answers it)
  int32_t target;               // the speed to reach, FULL units, after the dead zone
  int32_t out;                  // where the ramp is, FULL units
  uint16_t pause_ms;            // left of the pause between the directions
  uint8_t sent;                 // sent_e
  int16_t drive;                // the drive last sent (SENT_DRIVE)
  uint32_t poll_ms;             // time since the last current reading
  uint16_t stall_for_ms;        // how long the current has been at or over stall_mA
  bool reach_pending;           // a speed was asked for and REACHED is not published yet
  bool stalled;                 // a stall stopped the motor since the last SPEED
  bool current_failed;          // a current reading failed: the stall is not watched (reported once) until SETTINGS
} motor_t;

#define SYS_DEVICE_CTX motor_t

/* ---- The bridge ---- */

// Brake or coast now, once (not again while it already is).
static err_h motor_halt(motor_t* c, uint8_t mode) {
  const uint8_t kind = mode == DC_MOTOR_STOP_COAST ? SENT_COAST : SENT_BRAKE;
  if (c->sent == kind) {
    return NULL;
  }
  if (kind == SENT_COAST) {
    ERR_TRY(SYS_DEVICE_SET(c->cfg.bridge, CONTRACT_HBRIDGE, OP_COAST, .channel = c->cfg.channel));
  } else {
    ERR_TRY(SYS_DEVICE_SET(c->cfg.bridge, CONTRACT_HBRIDGE, OP_BRAKE, .channel = c->cfg.channel));
  }
  c->sent = kind;
  c->drive = 0;
  return NULL;
}

// The ramp's output onto the bridge: 0 stops (stop_mode); else min_pct + |out| x (max_pct - min_pct), the trim on
// top, never over max_pct; the sign is the direction (swapped by invert). Sent only when it changed.
static err_h motor_apply(motor_t* c) {
  const dc_motor_settings_t* s = &c->cfg.settings;
  if (c->out == 0) {
    return motor_halt(c, s->stop_mode);
  }
  const int32_t mag = c->out < 0 ? -c->out : c->out;
  int32_t pct = (int32_t)s->min_pct * (FULL / 100) + mag * ((int32_t)s->max_pct - s->min_pct) / 100;
  pct = pct * (100 + s->trim_pct) / 100;
  const int32_t ceiling = (int32_t)s->max_pct * (FULL / 100);
  if (pct > ceiling) {
    pct = ceiling;
  }
  int16_t drive = (int16_t)(pct * SYS_HBRIDGE_DRIVE_MAX / FULL);
  if ((c->out < 0) != (s->invert != 0)) {
    drive = (int16_t)-drive;
  }
  if (c->sent == SENT_DRIVE && drive == c->drive) {
    return NULL;
  }
  ERR_TRY(SYS_DEVICE_SET(c->cfg.bridge, CONTRACT_HBRIDGE, OP_DRIVE, .channel = c->cfg.channel, .drive = drive));
  c->sent = SENT_DRIVE;
  c->drive = drive;
  return NULL;
}

static err_h bridge_current(const motor_t* c, int32_t* mA) {
  return SYS_DEVICE_GET(c->cfg.bridge, CONTRACT_HBRIDGE, OP_CURRENT, mA, .channel = c->cfg.channel);
}

/* ---- The ramp ---- */

/* The time since the last step, from the board's clock: a step comes from the timer and from every write (SPEED,
 * STOP, SETTINGS), at no fixed rate, so the ramp moves by the time that passed, not by the count of steps. 0 for the
 * first step after the timer was stopped: that step starts the ramp, the next one moves it. */
static uint32_t elapsed_ms(motor_t* c) {
  const uint64_t now = board_time_uptime_ms();
  const uint64_t since = c->ticking ? now - c->stepped_ms : 0;
  c->stepped_ms = now;
  return since > UINT16_MAX ? UINT16_MAX : (uint32_t)since;
}

static void timer_stop(motor_t* c) {
  sys_device_timer_stop(&c->timer);
  c->ticking = false;
}

// One step of `ms`: towards the target, through 0 when the direction changes (and the pause there); away from 0 at
// the ramp-up rate, towards it at the ramp-down rate (a ramp of 0 ms: at once, whatever `ms`).
static void ramp_step(motor_t* c, uint32_t ms) {
  const dc_motor_settings_t* s = &c->cfg.settings;
  if (c->pause_ms != 0) {
    c->pause_ms = ms >= c->pause_ms ? 0 : (uint16_t)(c->pause_ms - ms);
    return;
  }
  const int32_t was = c->out;
  int32_t goal = c->target;
  if (was != 0 && goal != 0 && (goal > 0) != (was > 0)) {
    goal = 0;  // the other way: to 0 first
  }
  if (goal == was) {
    return;
  }
  const int32_t abs_goal = goal < 0 ? -goal : goal;
  const int32_t abs_was = was < 0 ? -was : was;
  const uint16_t ramp_ms = abs_goal > abs_was ? s->ramp_up_ms : s->ramp_down_ms;
  if (ramp_ms != 0 && ms == 0) {
    return;  // no time has passed
  }
  const int64_t by_time = ramp_ms == 0 ? 2 * FULL : (int64_t)FULL * ms / ramp_ms + 1;
  const int32_t step = by_time > 2 * FULL ? 2 * FULL : (int32_t)by_time;  // the whole range at most
  c->out = goal > was ? (was + step > goal ? goal : was + step) : (was - step < goal ? goal : was - step);
  if (was != 0 && c->out == 0 && c->target != 0 && (c->target > 0) != (was > 0)) {
    c->pause_ms = s->reverse_pause_ms;
  }
}

// While the motor turns and a stall current is set: the current every STALL_POLL_MS (`ms` passed since the last step).
// Over the limit for stall_ms stops the motor (stop_mode), latches `stalled` and publishes STALL. A failed reading
// stops the watch (reported once).
static err_h watch_stall(motor_t* c, uint32_t ms) {
  const dc_motor_settings_t* s = &c->cfg.settings;
  if (s->stall_mA == 0 || c->out == 0 || c->current_failed) {
    c->stall_for_ms = 0;
    return NULL;
  }
  c->poll_ms += ms;
  if (c->poll_ms < STALL_POLL_MS) {
    return NULL;
  }
  const uint32_t since = c->poll_ms;
  c->poll_ms = 0;
  int32_t mA = 0;
  err_h err = bridge_current(c, &mA);
  if (err != NULL) {
    c->current_failed = true;
    return err;
  }
  if ((mA < 0 ? -mA : mA) < (int32_t)s->stall_mA) {
    c->stall_for_ms = 0;
    return NULL;
  }
  const uint32_t stalled_for = c->stall_for_ms + since;
  c->stall_for_ms = stalled_for > UINT16_MAX ? UINT16_MAX : (uint16_t)stalled_for;
  if (c->stall_for_ms < s->stall_ms) {
    return NULL;
  }
  c->target_x10 = 0;
  c->target = c->out = 0;
  c->pause_ms = 0;
  c->stall_for_ms = 0;
  c->stalled = true;
  c->reach_pending = false;
  ERR_TRY(motor_halt(c, s->stop_mode));
  return SYS_EVENT_PUBLISH_DEFER(DC_MOTOR_EV_STALL, c->id, 0, .mA = mA);
}

/* One step now, by the time since the last one, then the timer runs while there is more to do: the ramp, the pause,
 * or a turning motor to watch (a start with the same period keeps its phase). At the target with a speed asked for:
 * REACHED, once. */
static err_h motor_tick(motor_t* c) {
  const uint32_t ms = elapsed_ms(c);
  ramp_step(c, ms);
  ERR_TRY(motor_apply(c));
  if (c->reach_pending && c->out == c->target && c->pause_ms == 0) {
    c->reach_pending = false;
    ERR_TRY(SYS_EVENT_PUBLISH_DEFER(DC_MOTOR_EV_REACHED, c->id, 0, .speed_x10 = c->target_x10));
  }
  ERR_TRY(watch_stall(c, ms));
  const bool watched = c->out != 0 && c->cfg.settings.stall_mA != 0 && !c->current_failed;
  if (c->out == c->target && c->pause_ms == 0 && !watched) {
    timer_stop(c);
  } else {
    sys_device_timer_start(&c->timer, TICK_MS);
    c->ticking = true;
  }
  return NULL;
}

// A speed asked for (SPEED, STOP, SETTINGS): the ramp goes there and REACHED follows.
static void set_target(motor_t* c, int16_t speed_x10) {
  c->target_x10 = speed_x10;
  c->reach_pending = true;
  const int32_t mag = speed_x10 < 0 ? -speed_x10 : speed_x10;
  c->target = mag < (int32_t)c->cfg.settings.deadzone_pct * 10 ? 0 : (int32_t)speed_x10 * X10_TO_FULL;
}

// Stops at once (no ramp): BRAKE, COAST, RESET, the safe state.
static err_h stop_now(motor_t* c, uint8_t mode) {
  c->reach_pending = false;
  c->target_x10 = 0;
  c->target = c->out = 0;
  c->pause_ms = 0;
  c->stall_for_ms = 0;
  timer_stop(c);
  return motor_halt(c, mode);
}

static err_h check_settings(uint8_t id, const dc_motor_settings_t* s, uint8_t one_way) {
  ERR_CHECK(s->invert <= 1 && !(one_way && s->invert), ERR_CMD_BAD_ARG);  // a half bridge has one direction
  SYS_DEV_CHECK_IN_RANGE(id, s->stop_mode, DC_MOTOR_STOP_BRAKE, DC_MOTOR_STOP_COAST);
  SYS_DEV_CHECK_IN_RANGE(id, s->max_pct, 1, 100);
  SYS_DEV_CHECK_IN_RANGE(id, s->min_pct, 0, s->max_pct < DC_MOTOR_MIN_PCT_MAX ? s->max_pct : DC_MOTOR_MIN_PCT_MAX);
  SYS_DEV_CHECK_IN_RANGE(id, s->deadzone_pct, 0, DC_MOTOR_DEADZONE_PCT_MAX);
  SYS_DEV_CHECK_IN_RANGE(id, s->trim_pct, -DC_MOTOR_TRIM_PCT_MAX, DC_MOTOR_TRIM_PCT_MAX);
  SYS_DEV_CHECK_IN_RANGE(id, s->ramp_up_ms, 0, DC_MOTOR_RAMP_MS_MAX);
  SYS_DEV_CHECK_IN_RANGE(id, s->ramp_down_ms, 0, DC_MOTOR_RAMP_MS_MAX);
  SYS_DEV_CHECK_IN_RANGE(id, s->reverse_pause_ms, 0, DC_MOTOR_PAUSE_MS_MAX);
  SYS_DEV_CHECK_IN_RANGE(id, s->stall_ms, DC_MOTOR_STALL_MS_MIN, DC_MOTOR_STALL_MS_MAX);
  return NULL;
}

/* ---- Own ops ---- */

/*******************************************************************************
 * OWN_DC_MOTOR : OP_SPEED (write)
 * Sets target speed (x10), clears any latched stall and steps the ramp.
 ******************************************************************************/
OP_DECLARE_WRITE(OWN_DC_MOTOR, OP_SPEED) {
  SYS_DEV_CHECK_IN_RANGE(c->id, args->speed_x10, c->cfg.one_way ? 0 : -DC_MOTOR_SPEED_MAX, DC_MOTOR_SPEED_MAX);
  set_target(c, args->speed_x10);
  c->stalled = false;
  return motor_tick(c);
}

/*******************************************************************************
 * OWN_DC_MOTOR : OP_SPEED (read)
 * Returns the current target speed (x10).
 ******************************************************************************/
OP_DECLARE_READ(OWN_DC_MOTOR, OP_SPEED) {
  return CMD_REPLY(OWN_DC_MOTOR, OP_SPEED, .speed_x10 = c->target_x10);
}

/*******************************************************************************
 * OWN_DC_MOTOR : OP_OUTPUT (read)
 * Returns the drive currently applied to the bridge.
 ******************************************************************************/
OP_DECLARE_READ(OWN_DC_MOTOR, OP_OUTPUT) {
  return CMD_REPLY(OWN_DC_MOTOR, OP_OUTPUT, c->out / X10_TO_FULL);
}

/*******************************************************************************
 * OWN_DC_MOTOR : OP_STOP (write)
 * Sets target speed to 0 and ramps down to a stop.
 ******************************************************************************/
OP_DECLARE_WRITE(OWN_DC_MOTOR, OP_STOP) {
  set_target(c, 0);
  return motor_tick(c);
}

/*******************************************************************************
 * OWN_DC_MOTOR : OP_BRAKE (write)
 * Immediate active brake, bypassing the ramp.
 ******************************************************************************/
OP_DECLARE_WRITE(OWN_DC_MOTOR, OP_BRAKE) {
  return stop_now(c, DC_MOTOR_STOP_BRAKE);
}

/*******************************************************************************
 * OWN_DC_MOTOR : OP_COAST (write)
 * Immediate coast release, bypassing the ramp.
 ******************************************************************************/
OP_DECLARE_WRITE(OWN_DC_MOTOR, OP_COAST) {
  return stop_now(c, DC_MOTOR_STOP_COAST);
}

/*******************************************************************************
 * OWN_DC_MOTOR : OP_SETTINGS (read)
 * Returns the current runtime motor settings.
 ******************************************************************************/
OP_DECLARE_READ(OWN_DC_MOTOR, OP_SETTINGS) {
  return cmd_reply(out, &c->cfg.settings, sizeof(c->cfg.settings));
}

/*******************************************************************************
 * OWN_DC_MOTOR : OP_SETTINGS (write)
 * Applies new motor settings at once (dead zone, ceiling and trim on current
 * target speed).
 ******************************************************************************/
OP_DECLARE_WRITE(OWN_DC_MOTOR, OP_SETTINGS) {
  ERR_TRY(check_settings(c->id, args, c->cfg.one_way));
  c->cfg.settings = *args;
  c->current_failed = false;
  set_target(c, c->target_x10);
  return motor_tick(c);
}

/*******************************************************************************
 * OWN_DC_MOTOR : OP_CURRENT (read)
 * Bridge motor current in mA; allowed while suspended.
 ******************************************************************************/
OP_DECLARE_READ(OWN_DC_MOTOR, OP_CURRENT) {
  int32_t mA = 0;
  ERR_TRY(bridge_current(c, &mA));
  return CMD_REPLY(OWN_DC_MOTOR, OP_CURRENT, mA);
}

/*******************************************************************************
 * OWN_DC_MOTOR : OP_STALLED (read)
 * Returns 1 if stall detection latched, else 0.
 ******************************************************************************/
OP_DECLARE_READ(OWN_DC_MOTOR, OP_STALLED) {
  return CMD_REPLY(OWN_DC_MOTOR, OP_STALLED, c->stalled ? 1 : 0);
}

/*******************************************************************************
 * OWN_DC_MOTOR : OP_TICK (write, LOCAL)
 * Timer ramp/stall step (runs only on a working motor; skipped while suspended).
 * A failed step stops the ramp until the next SPEED, STOP or SETTINGS.
 ******************************************************************************/
OP_DECLARE_WRITE(OWN_DC_MOTOR, OP_TICK) {
  const err_h err = motor_tick(c);
  if (err != NULL) {
    timer_stop(c);
  }
  return err;
}

/* ---- Lifecycle ---- */

/*******************************************************************************
 * CONTRACT_SYS_DEVICE : OP_RESET (write)
 * Restores initial settings, clears latched flags and stops in configured mode.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  c->cfg.settings = c->initial;
  c->stalled = false;
  c->current_failed = false;
  return stop_now(c, c->cfg.settings.stop_mode);
}

/*******************************************************************************
 * DC_MOTOR : suspend (lifecycle)
 * Fault safe state (and last state before REMOVE): releases drive immediately
 * via coast; stays stopped after resume until a new OP_SPEED.
 ******************************************************************************/
static err_h suspend(motor_t* c) {
  return stop_now(c, DC_MOTOR_STOP_COAST);
}

/*******************************************************************************
 * DC_MOTOR : resume (lifecycle)
 * Halts motor in configured stop_mode; a failing resume leaves it suspended.
 ******************************************************************************/
static err_h resume(motor_t* c) {
  return motor_halt(c, c->cfg.settings.stop_mode);
}

/* ---- Create ---- */

/*******************************************************************************
 * DC_MOTOR : create (init)
 * Validates config/settings, initializes OP_TICK timer, verifies bridge
 * OP_DRIVE availability and puts motor into initial stop_mode.
 ******************************************************************************/
static err_h create(uint8_t id, motor_t* c, const dc_motor_cfg_t* cfg) {
  c->id = id;
  ERR_TRY(sys_device_timer_init(id, &c->timer, OWN_DC_MOTOR_OP_TICK));
  ERR_CHECK(cfg->one_way <= 1, ERR_CMD_BAD_ARG);
  ERR_TRY(check_settings(id, &cfg->settings, cfg->one_way));
  c->cfg = *cfg;
  c->initial = cfg->settings;
  ERR_TRY(sys_device_require_op(id, cfg->bridge, CONTRACT_HBRIDGE_OP_DRIVE, CMD_DIR_WRITE));
  return motor_halt(c, cfg->settings.stop_mode);  // the bridge checks the channel; the motor starts stopped
}

/*******************************************************************************
 * DC_MOTOR : type registration (k_ops & SYS_DEVICE_TYPE_DEFINE)
 * Exposes lifecycle reset and all DC_MOTOR own ops (speed, output, stop/brake/
 * coast, settings, current, stalled, timer tick).
 ******************************************************************************/

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),
    OP_ENTRY(OWN_DC_MOTOR, OP_SPEED),    OP_ENTRY_R(OWN_DC_MOTOR, OP_OUTPUT),
    OP_ENTRY_W(OWN_DC_MOTOR, OP_STOP),   OP_ENTRY_W(OWN_DC_MOTOR, OP_BRAKE),   OP_ENTRY_W(OWN_DC_MOTOR, OP_COAST),
    OP_ENTRY(OWN_DC_MOTOR, OP_SETTINGS), OP_ENTRY_R(OWN_DC_MOTOR, OP_CURRENT), OP_ENTRY_R(OWN_DC_MOTOR, OP_STALLED),
    OP_ENTRY_W(OWN_DC_MOTOR, OP_TICK),
};

SYS_DEVICE_TYPE_DEFINE(DC_MOTOR, dc_motor_cfg_t, create, suspend, resume, k_ops)
