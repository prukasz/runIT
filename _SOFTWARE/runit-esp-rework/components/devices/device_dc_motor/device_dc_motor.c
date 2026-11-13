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

/* ----------------------------------------------------------------------------
 * Wire format: CREATE, the own ops, the own events
 * ------------------------------------------------------------------------- */

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

/** @brief CREATE's struct (wire format). */
typedef struct __attribute__((packed)) {
  uint8_t bridge;                /**< The motor driver's device ID (a device with the HBRIDGE contract) */
  uint8_t channel;               /**< Its channel the motor is on */
  uint8_t one_way;               /**< 1 = a half bridge: forward only */
  dc_motor_settings_t settings;  /**< The settings that change live */
} dc_motor_cfg_t;

/** @brief SPEED's value, both sides. */
typedef struct __attribute__((packed)) {
  int16_t speed_x10;  /**< Tenths of a percent: -1000 (full reverse) .. 1000 (full forward) */
} dc_motor_speed_t;

/* Own ops: X(P, NAME, n, key_t, value_t, flags), the read answers what the write takes.
 *   SPEED    - the speed to reach, both sides
 *   OUTPUT   - where the ramp is now, tenths of a percent
 *   STOP     - to 0 along the ramp, then stop_mode
 *   BRAKE    - at once, no ramp: shorts the motor
 *   COAST    - at once, no ramp: releases the motor
 *   SETTINGS - the live settings, both sides
 *   CURRENT  - the bridge channel's current in mA
 *   STALLED  - 1 = stopped by a stall since the last SPEED
 *   TICK     - the ramp's step, internal: the board timers' task runs it (a step, LOCAL), never while suspended */
#define DC_MOTOR_OPERATIONS(X, P)                                       \
  X(P, SPEED,    0x00, cmd_none_t, dc_motor_speed_t,    0)              \
  X(P, OUTPUT,   0x01, cmd_none_t, int32_t,             0)              \
  X(P, STOP,     0x02, cmd_none_t, cmd_none_t,          0)              \
  X(P, BRAKE,    0x03, cmd_none_t, cmd_none_t,          0)              \
  X(P, COAST,    0x04, cmd_none_t, cmd_none_t,          0)              \
  X(P, SETTINGS, 0x05, cmd_none_t, dc_motor_settings_t, 0)              \
  X(P, CURRENT,  0x06, cmd_none_t, int32_t,             0)              \
  X(P, STALLED,  0x07, cmd_none_t, uint8_t,             0)              \
  X(P, TICK,     0x08, cmd_none_t, cmd_none_t,          SYS_DEVICE_OP_LOCAL)

SYS_DEVICE_OWN_DECLARE(DC_MOTOR)

/* Own events: module SYS_EVENT_MODULE_TYPE(DC_MOTOR_TYPE), source = the motor, channel 0. Both are published from an
 * op, under the motor's lock (not recursive), most from TICK in the board timers' task: always deferred, so no
 * listener runs under that lock (one calling the motor back would be refused) nor holds up the next steps.
 *   STALL   - a stall stopped the motor; the payload the bridge channel's current then. NOTIFY: the user must see it.
 *   REACHED - the motor is at the speed asked for (SPEED, STOP, SETTINGS; a dead zone's 0 included), once per ask,
 *             also when it was there already; the payload that speed. Not after BRAKE / COAST / RESET / a suspend (at
 *             once, nothing to wait for) nor a stall (STALL instead). Code flow (a program waits for it), not
 *             something to show by itself. */
#define DC_MOTOR_EVENTS(X, P)                                   \
  X(P, STALL,   0x01, SYS_EVENT_NOTIFY, { int32_t mA; })        \
  X(P, REACHED, 0x02, 0,                { int16_t speed_x10; })

SYS_DEVICE_OWN_EVENTS(DC_MOTOR)

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
  uint8_t id;                   // the device's ID, given to create()
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
    ERR_TRY(SYS_DEVICE_SET(c->cfg.bridge, HBRIDGE, COAST, .channel = c->cfg.channel));
  } else {
    ERR_TRY(SYS_DEVICE_SET(c->cfg.bridge, HBRIDGE, BRAKE, .channel = c->cfg.channel));
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
  ERR_TRY(SYS_DEVICE_SET(c->cfg.bridge, HBRIDGE, DRIVE, .channel = c->cfg.channel, .drive = drive));
  c->sent = SENT_DRIVE;
  c->drive = drive;
  return NULL;
}

static err_h bridge_current(const motor_t* c, int32_t* mA) {
  return SYS_DEVICE_GET(c->cfg.bridge, HBRIDGE, CURRENT, mA, .channel = c->cfg.channel);
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

OP_WRITE(DC_MOTOR, SPEED) {
  SYS_DEV_CHECK_IN_RANGE(c->id, args->speed_x10, c->cfg.one_way ? 0 : -DC_MOTOR_SPEED_MAX, DC_MOTOR_SPEED_MAX);
  set_target(c, args->speed_x10);
  c->stalled = false;
  return motor_tick(c);
}

OP_READ(DC_MOTOR, SPEED) {
  return CMD_REPLY(DC_MOTOR, SPEED, .speed_x10 = c->target_x10);
}

OP_READ(DC_MOTOR, OUTPUT) {
  return CMD_REPLY(DC_MOTOR, OUTPUT, c->out / X10_TO_FULL);
}

OP_WRITE(DC_MOTOR, STOP) {
  set_target(c, 0);
  return motor_tick(c);
}

OP_WRITE(DC_MOTOR, BRAKE) {
  return stop_now(c, DC_MOTOR_STOP_BRAKE);
}

OP_WRITE(DC_MOTOR, COAST) {
  return stop_now(c, DC_MOTOR_STOP_COAST);
}

OP_READ(DC_MOTOR, SETTINGS) {
  return cmd_reply(out, &c->cfg.settings, sizeof(c->cfg.settings));
}

// Takes effect at once: the dead zone on the speed already set, the ceiling and the trim on the drive.
OP_WRITE(DC_MOTOR, SETTINGS) {
  ERR_TRY(check_settings(c->id, args, c->cfg.one_way));
  c->cfg.settings = *args;
  c->current_failed = false;
  set_target(c, c->target_x10);
  return motor_tick(c);
}

// A read: allowed while suspended (the bridge answers its current then too).
OP_READ(DC_MOTOR, CURRENT) {
  int32_t mA = 0;
  ERR_TRY(bridge_current(c, &mA));
  return CMD_REPLY(DC_MOTOR, CURRENT, mA);
}

OP_READ(DC_MOTOR, STALLED) {
  return CMD_REPLY(DC_MOTOR, STALLED, c->stalled ? 1 : 0);
}

// The timer's step, only on a working motor (the registry skips it while suspended). A step after the timer stopped
// (one on its way) finds nothing to do and stops it again. A failed step stops the ramp, so it is reported once; the
// next SPEED, STOP or SETTINGS starts it again.
OP_WRITE(DC_MOTOR, TICK) {
  const err_h err = motor_tick(c);
  if (err != NULL) {
    timer_stop(c);
  }
  return err;
}

/* ---- Lifecycle ---- */

OP_WRITE(SYS_DEVICE, RESET) {
  c->cfg.settings = c->initial;
  c->stalled = false;
  c->current_failed = false;
  return stop_now(c, c->cfg.settings.stop_mode);
}

// Fault safe state, and the motor's last state before REMOVE: no drive, the motor released at once; it stays stopped
// after RESUME until a new SPEED.
static err_h suspend(motor_t* c) {
  return stop_now(c, DC_MOTOR_STOP_COAST);
}

// A failing resume leaves the device suspended.
static err_h resume(motor_t* c) {
  return motor_halt(c, c->cfg.settings.stop_mode);
}

/* ---- Create ---- */

static err_h create(uint8_t id, motor_t* c, const dc_motor_cfg_t* cfg) {
  c->id = id;
  ERR_TRY(sys_device_timer_init(id, &c->timer, DC_MOTOR_OP_TICK));
  ERR_CHECK(cfg->one_way <= 1, ERR_CMD_BAD_ARG);
  ERR_TRY(check_settings(id, &cfg->settings, cfg->one_way));
  c->cfg = *cfg;
  c->initial = cfg->settings;
  ERR_TRY(sys_device_require_op(id, cfg->bridge, HBRIDGE_OP_DRIVE, CMD_DIR_WRITE));
  return motor_halt(c, cfg->settings.stop_mode);  // the bridge checks the channel; the motor starts stopped
}

/* ---- Type ---- */

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(SYS_DEVICE, RESET),
    OP_ENTRY(DC_MOTOR, SPEED),    OP_ENTRY_R(DC_MOTOR, OUTPUT),
    OP_ENTRY_W(DC_MOTOR, STOP),   OP_ENTRY_W(DC_MOTOR, BRAKE),   OP_ENTRY_W(DC_MOTOR, COAST),
    OP_ENTRY(DC_MOTOR, SETTINGS), OP_ENTRY_R(DC_MOTOR, CURRENT), OP_ENTRY_R(DC_MOTOR, STALLED),
    OP_ENTRY_W(DC_MOTOR, TICK),
};

SYS_DEVICE_TYPE_DEFINE(DC_MOTOR, dc_motor_cfg_t, create, suspend, resume, k_ops)
