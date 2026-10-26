#include "device_dc_motor.h"
#include "esp_timer.h"

#define TICK_MS          10u      // one step of the ramp
#define STALL_POLL_TICKS 5u       // the current is read every 50 ms while the motor turns
#define FULL             100000   // the ramp's unit: 100 % (fine enough that a 10 s ramp still moves every tick)
#define X10_TO_FULL      (FULL / DC_MOTOR_SPEED_MAX)

// What the bridge channel was last told: a drive, a brake, a coast (nothing yet: not driven).
typedef enum { SENT_NONE = 0, SENT_DRIVE, SENT_BRAKE, SENT_COAST } sent_e;

typedef struct {
  sys_device_base_t base;         // first: id, suspended (the motor stopped)
  dc_motor_cfg_t cfg;             // the live settings are cfg.settings
  dc_motor_settings_t initial;    // the settings create took: RESET returns to them
  esp_timer_handle_t timer;       // the ramp's tick, running while the motor moves or is watched
  bool timer_running;
  int16_t target_x10;             // the speed as written (SPEED's read answers it)
  int32_t target;                 // the speed to reach, FULL units, after the dead zone
  int32_t out;                    // where the ramp is, FULL units
  uint16_t pause_ms;              // left of the pause between the directions
  uint8_t sent;                   // sent_e
  int16_t drive;                  // the drive last sent (SENT_DRIVE)
  uint8_t stall_poll;             // ticks since the last current reading
  uint16_t stall_for_ms;          // how long the current has been at or over stall_mA
  bool stalled;                   // a stall stopped the motor since the last SPEED
  bool current_failed;            // a current reading failed: the stall is not watched (reported once) until SETTINGS
} motor_t;

/* ---- The bridge ---- */

// Brake or coast now, once (not again while it already is).
static err_h motor_halt(motor_t* c, uint8_t mode) {
  const uint8_t kind = mode == DC_MOTOR_STOP_COAST ? SENT_COAST : SENT_BRAKE;
  if (c->sent == kind) {
    return NULL;
  }
  ERR_TRY(kind == SENT_COAST ? sys_hbridge_coast(c->cfg.bridge, c->cfg.channel)
                             : sys_hbridge_brake(c->cfg.bridge, c->cfg.channel));
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
  ERR_TRY(sys_hbridge_set_drive(c->cfg.bridge, c->cfg.channel, drive));
  c->sent = SENT_DRIVE;
  c->drive = drive;
  return NULL;
}

/* ---- The ramp ---- */

// One step of `ms`: towards the target, through 0 when the direction changes (and the pause there); away from 0 at
// the ramp-up rate, towards it at the ramp-down rate.
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
  const int32_t step = ramp_ms == 0 ? 2 * FULL : (int32_t)((int64_t)FULL * ms / ramp_ms) + 1;
  c->out = goal > was ? (was + step > goal ? goal : was + step) : (was - step < goal ? goal : was - step);
  if (was != 0 && c->out == 0 && c->target != 0 && (c->target > 0) != (was > 0)) {
    c->pause_ms = s->reverse_pause_ms;
  }
}

// While the motor turns and a stall current is set: the current every STALL_POLL_TICKS. Over the limit for stall_ms
// stops the motor (stop_mode), latches `stalled` and publishes STALL. A failed reading stops the watch (reported once).
static err_h watch_stall(motor_t* c) {
  const dc_motor_settings_t* s = &c->cfg.settings;
  if (s->stall_mA == 0 || c->out == 0 || c->current_failed) {
    c->stall_for_ms = 0;
    return NULL;
  }
  if (++c->stall_poll < STALL_POLL_TICKS) {
    return NULL;
  }
  c->stall_poll = 0;
  int32_t mA = 0;
  err_h err = sys_hbridge_get_current_mA(c->cfg.bridge, c->cfg.channel, &mA);
  if (err != NULL) {
    c->current_failed = true;
    return err;
  }
  if ((mA < 0 ? -mA : mA) < (int32_t)s->stall_mA) {
    c->stall_for_ms = 0;
    return NULL;
  }
  c->stall_for_ms = (uint16_t)(c->stall_for_ms + STALL_POLL_TICKS * TICK_MS);
  if (c->stall_for_ms < s->stall_ms) {
    return NULL;
  }
  c->target_x10 = 0;
  c->target = c->out = 0;
  c->pause_ms = 0;
  c->stall_for_ms = 0;
  c->stalled = true;
  ERR_TRY(motor_halt(c, s->stop_mode));
  return SYS_EVENT_PUBLISH(DC_MOTOR, STALL, c->base.id, 0, mA);
}

static err_h timer_stop(motor_t* c) {
  if (!c->timer_running) {
    return NULL;
  }
  c->timer_running = false;
  esp_err_t rc = esp_timer_stop(c->timer);
  return rc == ESP_ERR_INVALID_STATE ? NULL : ERR_ESP(rc);
}

// One step now, then the timer runs while there is more to do: the ramp, the pause, or a turning motor to watch.
static err_h motor_tick(motor_t* c) {
  ramp_step(c, TICK_MS);
  ERR_TRY(motor_apply(c));
  ERR_TRY(watch_stall(c));
  const bool watched = c->out != 0 && c->cfg.settings.stall_mA != 0 && !c->current_failed;
  if (c->out == c->target && c->pause_ms == 0 && !watched) {
    return timer_stop(c);
  }
  if (!c->timer_running) {
    ERR_TRY(ERR_ESP(esp_timer_start_periodic(c->timer, (uint64_t)TICK_MS * 1000u)));
    c->timer_running = true;
  }
  return NULL;
}

// esp_timer task. Only the ID crosses over: a REMOVE stops the timer first, and a tick already waiting for the lock
// then finds no device (ERR_DEV_NOT_FOUND, reported).
static void tick_cb(void* arg) {
  err_report(sys_device_write((uint16_t)(uintptr_t)arg, DC_MOTOR_OP_TICK, NULL, 0));
}

static void set_target(motor_t* c, int16_t speed_x10) {
  c->target_x10 = speed_x10;
  const int32_t mag = speed_x10 < 0 ? -speed_x10 : speed_x10;
  c->target = mag < (int32_t)c->cfg.settings.deadzone_pct * 10 ? 0 : (int32_t)speed_x10 * X10_TO_FULL;
}

// Stops at once (no ramp): BRAKE, COAST, the safe state.
static err_h stop_now(motor_t* c, uint8_t mode) {
  c->target_x10 = 0;
  c->target = c->out = 0;
  c->pause_ms = 0;
  c->stall_for_ms = 0;
  ERR_TRY(timer_stop(c));
  return motor_halt(c, mode);
}

static err_h check_settings(const motor_t* c, const dc_motor_settings_t* s, uint8_t one_way) {
  const uint16_t id = c->base.id;
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

SYS_DEVICE_HANDLER(set_speed_h, dc_motor_speed_t) {
  (void)out, (void)out_cap, (void)out_len;
  motor_t* c = ctx;
  SYS_DEV_CHECK_IN_RANGE(c->base.id, args->speed_x10, c->cfg.one_way ? 0 : -DC_MOTOR_SPEED_MAX, DC_MOTOR_SPEED_MAX);
  set_target(c, args->speed_x10);
  c->stalled = false;
  return motor_tick(c);
}

SYS_DEVICE_HANDLER(get_speed_h, sys_device_none_t) {
  (void)args;
  const motor_t* c = ctx;
  const dc_motor_speed_t speed = {.speed_x10 = c->target_x10};
  return cmd_reply(out, out_cap, out_len, &speed, sizeof(speed));
}

SYS_DEVICE_HANDLER(get_output_h, sys_device_none_t) {
  (void)args;
  const motor_t* c = ctx;
  const int32_t output_x10 = c->out / X10_TO_FULL;
  return cmd_reply(out, out_cap, out_len, &output_x10, sizeof(output_x10));
}

SYS_DEVICE_HANDLER(stop_h, sys_device_none_t) {
  (void)args, (void)out, (void)out_cap, (void)out_len;
  motor_t* c = ctx;
  set_target(c, 0);
  return motor_tick(c);
}

SYS_DEVICE_HANDLER(brake_h, sys_device_none_t) {
  (void)args, (void)out, (void)out_cap, (void)out_len;
  return stop_now(ctx, DC_MOTOR_STOP_BRAKE);
}

SYS_DEVICE_HANDLER(coast_h, sys_device_none_t) {
  (void)args, (void)out, (void)out_cap, (void)out_len;
  return stop_now(ctx, DC_MOTOR_STOP_COAST);
}

SYS_DEVICE_HANDLER(get_settings_h, sys_device_none_t) {
  (void)args;
  const motor_t* c = ctx;
  return cmd_reply(out, out_cap, out_len, &c->cfg.settings, sizeof(c->cfg.settings));
}

// Takes effect at once: the dead zone on the speed already set, the ceiling and the trim on the drive.
SYS_DEVICE_HANDLER(set_settings_h, dc_motor_settings_t) {
  (void)out, (void)out_cap, (void)out_len;
  motor_t* c = ctx;
  ERR_TRY(check_settings(c, args, c->cfg.one_way));
  c->cfg.settings = *args;
  c->current_failed = false;
  set_target(c, c->target_x10);
  return motor_tick(c);
}

// A read: allowed while suspended (the bridge answers its current then too).
SYS_DEVICE_HANDLER(get_current_h, sys_device_none_t) {
  (void)args;
  const motor_t* c = ctx;
  int32_t mA = 0;
  ERR_TRY(sys_hbridge_get_current_mA(c->cfg.bridge, c->cfg.channel, &mA));
  return cmd_reply(out, out_cap, out_len, &mA, sizeof(mA));
}

SYS_DEVICE_HANDLER(get_stalled_h, sys_device_none_t) {
  (void)args;
  const motor_t* c = ctx;
  const uint8_t stalled = c->stalled ? 1 : 0;
  return cmd_reply(out, out_cap, out_len, &stalled, sizeof(stalled));
}

// The timer's step: runs while suspended too (the registry lets it through), and then does nothing.
SYS_DEVICE_HANDLER(tick_h, sys_device_none_t) {
  (void)args, (void)out, (void)out_cap, (void)out_len;
  motor_t* c = ctx;
  if (c->base.suspended) {
    return timer_stop(c);
  }
  return motor_tick(c);
}

/* ---- Lifecycle ---- */

// Releases the timer and the motor (coast); REMOVE and the create rollback.
static err_h uninstall(void* ctx) {
  motor_t* c = ctx;
  err_h err = NULL;
  if (c->timer != NULL) {
    ERR_KEEP_FIRST(err, timer_stop(c));
    ERR_KEEP_FIRST(err, ERR_ESP(esp_timer_delete(c->timer)));
    c->timer = NULL;
  }
  if (c->sent != SENT_NONE) {
    ERR_KEEP_FIRST(err, sys_hbridge_coast(c->cfg.bridge, c->cfg.channel));
    c->sent = SENT_NONE;
  }
  return err;
}

OP_WRITE(SYS_DEVICE, RESET) {
  motor_t* c = ctx;
  c->cfg.settings = c->initial;
  c->stalled = false;
  c->current_failed = false;
  return stop_now(c, c->cfg.settings.stop_mode);
}

// Fault safe state: no drive, the motor released at once; it stays stopped after RESUME until a new SPEED.
static err_h suspend_hook(void* ctx) {
  return stop_now(ctx, DC_MOTOR_STOP_COAST);
}

static err_h resume_hook(void* ctx) {
  motor_t* c = ctx;
  return motor_halt(c, c->cfg.settings.stop_mode);
}

/* ---- Create ---- */

static err_h create(void* ctx, const void* cfg_in) {
  motor_t* c = ctx;
  const dc_motor_cfg_t* cfg = cfg_in;
  ERR_CHECK(cfg->one_way <= 1, ERR_CMD_BAD_ARG);
  ERR_TRY(check_settings(c, &cfg->settings, cfg->one_way));
  c->cfg = *cfg;
  c->initial = cfg->settings;
  ERR_TRY(sys_device_use(&c->base, cfg->bridge));

  const esp_timer_create_args_t timer_args = {
      .callback = tick_cb, .arg = (void*)(uintptr_t)c->base.id, .name = "dc_motor"};
  ERR_TRY(ERR_ESP(esp_timer_create(&timer_args, &c->timer)));
  return motor_halt(c, cfg->settings.stop_mode);  // the bridge checks the channel; the motor starts stopped
}

/* ---- Type ---- */

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(SYS_DEVICE, RESET),
    /* Device own ops */
    {.op = DC_MOTOR_OP_SPEED, .read = get_speed_h, .write = set_speed_h},
    {.op = DC_MOTOR_OP_OUTPUT, .read = get_output_h},
    {.op = DC_MOTOR_OP_STOP, .write = stop_h},
    {.op = DC_MOTOR_OP_BRAKE, .write = brake_h},
    {.op = DC_MOTOR_OP_COAST, .write = coast_h},
    {.op = DC_MOTOR_OP_SETTINGS, .read = get_settings_h, .write = set_settings_h},
    {.op = DC_MOTOR_OP_CURRENT, .read = get_current_h},
    {.op = DC_MOTOR_OP_STALLED, .read = get_stalled_h},
    /* Internal ramp timer op */
    {.op = DC_MOTOR_OP_TICK, .write = tick_h, .always = true},
};

static const sys_device_type_t k_type = {
    .type      = SYS_DEVICE_TYPE_DC_MOTOR,
    .cfg_size  = sizeof(dc_motor_cfg_t),
    .ctx_size  = sizeof(motor_t),
    .create    = create,
    .uninstall = uninstall,
    .suspend   = suspend_hook,
    .resume    = resume_hook,
    .ops       = k_ops,
    .op_count  = ARRAY_LEN(k_ops),
};

SYS_DEVICE_TYPE_REGISTER(k_type)
