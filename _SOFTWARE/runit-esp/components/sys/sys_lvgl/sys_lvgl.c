#include "sys_lvgl.h"

#include "board_time.h"
#include "esp_log.h"
#include "lvgl.h"
#include "sdkconfig.h"
#include "sys_display_contract.h"
#include "sys_lvgl_assert.h"
#include "sys_lvgl_pool.h"
#include "sys_touch_contract.h"
#include "utils.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

ERR_MODULE_DEFINE(ERR_MOD_SYS_LVGL, "sys_lvgl", ERR_SYS_LVGL_MAP)

static const char* TAG = "sys_lvgl";

/* What the LVGL task takes on its next wake, set from other tasks (s_pending). */
#define BIT_DISPLAY_STATE (1u << 0)  // the display was suspended, resumed or reset: read INFO, set the rotation again
#define BIT_INVALIDATE    (1u << 1)  // redraw the whole screen (after a failed FLUSH; never inside the rendering)

typedef struct {
  bool installed;
  bool lv_up;              // lv_init ran on the pool: from INSTALL to RESET
  bool stopped;            // STOP
  bool paused;             // the display is suspended
  uint16_t display;
  uint16_t touch;
  uint32_t heap_bytes;     // INSTALL's: the pool's size
  uint8_t rotation;        // sys_display_orientation_e = the lv_display's rotation
  uint8_t bpp;             // bytes per pixel of the display's format
  uint8_t buffers;
  uint32_t buffer_bytes;
  void* buf[2];
  lv_display_t* disp;
  lv_indev_t* indev;
  bool in_flight;          // a FLUSH was accepted and its end not taken yet
  uint32_t seq;            // the number of the last FLUSH
  bool flush_failing;      // a FLUSH failed and none was accepted since: reported once
  bool touch_failing;      // the same for POINT
  bool health_off;         // the display answered HEALTH UNKNOWN (not readable): not checked again until INSTALL
  bool display_lost;       // HEALTH found no answer: reported once, the screen redrawn when it answers again
  TickType_t health_at;    // the next HEALTH check
  uint32_t flushes;
  uint32_t flush_errors;
  sys_lvgl_reset_f reset_hook;
} lvgl_t;

static lvgl_t s;                       // under the LVGL lock (s_lock)
static void* s_pool;                   // LVGL's one block, INSTALL to RESET

_Static_assert(SYS_LVGL_HEAP_MIN == LV_MEM_SIZE, "SYS_LVGL_HEAP_MIN is lv_conf.h's LV_MEM_SIZE");
_Static_assert(SYS_LVGL_HEAP_MAX == LV_MEM_SIZE + LV_MEM_POOL_EXPAND_SIZE, "SYS_LVGL_HEAP_MAX is lv_conf.h's");
static volatile uint32_t s_flight;     // the number of the FLUSH whose done is awaited; 0 none (read by the ISR)
static uint16_t s_display_id = SYS_DEVICE_ID_NONE;  // s.display for sys_lvgl_device_state (atomic)
static uint32_t s_pending;             // BIT_* (atomic)

U_RECURSIVE_MUTEX_DEFINE(s_lock);  // the LVGL lock: LVGL runs without an OS of its own (LV_OS_NONE)
U_BINARY_SEM_DEFINE(s_flush_done);
U_TASK_DEFINE(s_lvgl_task, CONFIG_SYS_LVGL_TASK_STACK);

/* ---- platform ---- */

void sys_lvgl_assert_failed(void) {
  ESP_LOGE(TAG, "LVGL assert");
  abort();
}

static void log_cb(lv_log_level_t level, const char* text) {
  if (level >= LV_LOG_LEVEL_ERROR) {
    ESP_LOGE(TAG, "%s", text);
  } else {
    ESP_LOGW(TAG, "%s", text);
  }
}

static uint32_t tick_cb(void) {
  return (uint32_t)board_time_uptime_ms();
}

/* LV_MEM_POOL_ALLOC (lv_conf.h): lv_init builds its TLSF heap on the first LV_MEM_SIZE bytes of INSTALL's block. */
void* sys_lvgl_pool_take(size_t size) {
  (void)size;
  return s_pool;
}

// LVGL's pool: internal RAM, 8-byte aligned (TLSF).
static void* pool_alloc(uint32_t bytes) {
#ifdef ESP_PLATFORM
  return heap_caps_aligned_alloc(8, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
  return malloc(bytes);
#endif
}

// A draw buffer: DMA-capable internal RAM, 4-byte aligned.
static void* buffer_alloc(uint32_t bytes) {
#ifdef ESP_PLATFORM
  return heap_caps_aligned_alloc(4, bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
#else
  return malloc(bytes);
#endif
}

static void buffer_free(void* p) {
#ifdef ESP_PLATFORM
  heap_caps_free(p);
#else
  free(p);
#endif
}

static void post(uint32_t bits) {
  __atomic_fetch_or(&s_pending, bits, __ATOMIC_RELAXED);
  if (s_lvgl_task != NULL) {
    xTaskNotifyGive(s_lvgl_task);
  }
}

/* ---- the end of a FLUSH ---- */

// The FLUSH's done, from the transfer-done ISR: only the FLUSH awaited gives the semaphore.
static void flush_done(void* ctx) {
  if ((uint32_t)(uintptr_t)ctx != s_flight) {
    return;
  }
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(s_flush_done, &woken);
  portYIELD_FROM_ISR(woken);
}

// Waits for the end of the FLUSH in flight, at most the limit; past it: FLUSH_TIMEOUT, RESET of the display, a redraw.
static void wait_flush_end(void) {
  if (!s.in_flight) {
    return;
  }
  s.in_flight = false;
  if (xSemaphoreTake(s_flush_done, MSEC(CONFIG_SYS_LVGL_FLUSH_TIMEOUT_MS)) == pdTRUE) {
    return;
  }
  s_flight = 0;
  s.flush_errors++;
  err_report(ERR_NEW(ERR_DISPLAY_FLUSH_TIMEOUT, .id = s.display, .timeout_ms = CONFIG_SYS_LVGL_FLUSH_TIMEOUT_MS));
  err_report(sys_device_write(s.display, SYS_DEVICE_OP_RESET, NULL, 0));
  err_report(sys_display_set_orientation(s.display, s.rotation));  // the rest of this frame draws in it
  post(BIT_INVALIDATE);
}

/* ---- LVGL's callbacks (the LVGL task, under the LVGL lock) ---- */

static void flush_failed(err_h err) {
  if (err_root_tag(err) == ERR_DEV_SUSPENDED) {  // before the listener's bit came
    err_free(err);
    post(BIT_DISPLAY_STATE);
    return;
  }
  s.flush_errors++;
  if (s.flush_failing) {
    err_free(err);
  } else {
    s.flush_failing = true;
    err_report(err);
  }
  post(BIT_INVALIDATE);
}

static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  const display_window_t window = {
      .x1 = (uint16_t)area->x1, .y1 = (uint16_t)area->y1, .x2 = (uint16_t)area->x2, .y2 = (uint16_t)area->y2};
  const uint32_t len = (uint32_t)lv_area_get_size(area) * s.bpp;
  (void)xSemaphoreTake(s_flush_done, NO_WAIT);  // a give of a FLUSH given up on
  s.seq = s.seq == UINT32_MAX ? 1 : s.seq + 1;
  s_flight = s.seq;
  err_h err = sys_display_flush(s.display, px_map, len, &window, flush_done, (void*)(uintptr_t)s.seq);
  if (err == NULL) {
    s.in_flight = true;
    s.flushes++;
    s.flush_failing = false;
    return;
  }
  s_flight = 0;
  flush_failed(err);
  lv_display_flush_ready(disp);
}

static void flush_wait_cb(lv_display_t* disp) {
  (void)disp;
  wait_flush_end();
}

static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
  (void)indev;
  touch_point_t p;
  err_h err = sys_touch_get_point(s.touch, &p);
  if (err != NULL) {
    data->state = LV_INDEV_STATE_RELEASED;
    if (s.touch_failing) {
      err_free(err);
    } else {
      s.touch_failing = true;
      err_report(err);
    }
    return;
  }
  s.touch_failing = false;
  data->point.x = p.x;
  data->point.y = p.y;
  data->state = p.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* ---- state (under the LVGL lock) ---- */

static bool drawing(void) {
  return s.installed && !s.stopped && !s.paused;
}

static void invalidate(void) {
  if (s.disp != NULL) {
    lv_obj_invalidate(lv_display_get_screen_active(s.disp));
  }
}

static void apply_drawing(void) {
  if (s.indev != NULL) {
    lv_indev_enable(s.indev, drawing());
  }
}

// The display's state changed: paused while it is suspended; otherwise its rotation again and a redraw.
static void display_state_changed(void) {
  sys_device_info_t info;
  err_h err = sys_device_read(s.display, SYS_DEVICE_OP_INFO, NULL, 0, &info, sizeof(info));
  if (err != NULL) {
    err_report(err);
    return;
  }
  s.paused = info.suspended != 0;
  if (!s.paused) {
    wait_flush_end();
    err_report(sys_display_set_orientation(s.display, s.rotation));
    invalidate();
  }
  apply_drawing();
}

// Every CONFIG_SYS_LVGL_HEALTH_MS while drawing: the display's HEALTH, between two FLUSHes. No answer: reported once,
// the screen redrawn when it answers again (a loose data wire leaves the picture wrong). Restarted (it lost its power):
// reported, RESET of the display, its rotation set again, the screen redrawn.
static void check_health(void) {
  if (CONFIG_SYS_LVGL_HEALTH_MS == 0 || s.health_off) {
    return;
  }
  const TickType_t now = xTaskGetTickCount();
  if ((int32_t)(now - s.health_at) < 0) {
    return;
  }
  s.health_at = now + MSEC(CONFIG_SYS_LVGL_HEALTH_MS);
  wait_flush_end();
  display_health_t h;
  err_h err = sys_display_get_health(s.display, &h);
  if (err != NULL) {
    err_report(err);
    return;
  }
  if (h.health == SYS_DISPLAY_HEALTH_UNKNOWN) {
    s.health_off = true;
  } else if (h.health == SYS_DISPLAY_HEALTH_NO_ANSWER) {
    if (!s.display_lost) {
      s.display_lost = true;
      err_report(ERR_NEW(ERR_DISPLAY_LOST, .id = s.display, .health = h.health, .power_mode = h.power_mode));
    }
  } else if (h.health == SYS_DISPLAY_HEALTH_RESTARTED) {
    if (!s.display_lost) {
      err_report(ERR_NEW(ERR_DISPLAY_LOST, .id = s.display, .health = h.health, .power_mode = h.power_mode));
    }
    s.display_lost = false;
    err_report(sys_device_write(s.display, SYS_DEVICE_OP_RESET, NULL, 0));
    err_report(sys_display_set_orientation(s.display, s.rotation));
    invalidate();
  } else if (s.display_lost) {
    s.display_lost = false;
    invalidate();
  }
}

static void take_bits(uint32_t bits) {
  if (!s.installed) {
    return;
  }
  if ((bits & BIT_DISPLAY_STATE) != 0u) {
    display_state_changed();
  }
  if ((bits & BIT_INVALIDATE) != 0u) {
    invalidate();
  }
}

/* The LVGL task: the bits, then LVGL's timers (rendering, the touch) while drawing, all under the LVGL lock, so no
   frame starts after STOP or RESET returns. Sleeps until LVGL's next timer, at most one refresh period; while not
   drawing until a bit or START. */
static void lvgl_task(void* arg) {
  (void)arg;
  TickType_t wait = portMAX_DELAY;
  while (true) {
    (void)ulTaskNotifyTake(pdTRUE, wait);
    const uint32_t bits = __atomic_exchange_n(&s_pending, 0u, __ATOMIC_RELAXED);
    sys_lvgl_lock();
    take_bits(bits);
    wait = portMAX_DELAY;
    if (drawing()) {
      check_health();
      const uint32_t next = lv_timer_handler();
      wait = MSEC(next == 0u ? 1u : next < LV_DEF_REFR_PERIOD ? next : LV_DEF_REFR_PERIOD);
    }
    sys_lvgl_unlock();
  }
}

/* ---- INSTALL, RESET ---- */

// Releases whatever INSTALL built, LVGL itself and its pool; empty afterwards (LVGL lock held).
static void teardown(void) {
  wait_flush_end();
  if (s.indev != NULL) {
    lv_indev_delete(s.indev);
  }
  if (s.disp != NULL) {
    lv_display_delete(s.disp);
  }
  if (s.lv_up) {
    lv_deinit();  // whatever LVGL still holds is in the pool, freed with it
  }
#ifdef ESP_PLATFORM
  heap_caps_free(s_pool);
#else
  free(s_pool);
#endif
  s_pool = NULL;
  for (uint8_t i = 0; i < 2; i++) {
    buffer_free(s.buf[i]);
  }
  const sys_lvgl_reset_f hook = s.reset_hook;
  memset(&s, 0, sizeof(s));
  s.reset_hook = hook;
  s.display = SYS_DEVICE_ID_NONE;
  s.touch = SYS_DEVICE_ID_NONE;
  __atomic_store_n(&s_display_id, SYS_DEVICE_ID_NONE, __ATOMIC_RELAXED);
}

// One draw buffer: a tenth of the screen in whole rows of its longer side, at least one row, at most the display's
// largest FLUSH and CONFIG_SYS_LVGL_BUFFER_MAX.
static err_h buffer_size(uint16_t id, uint16_t w, uint16_t h, uint32_t flush_max, uint32_t* out) {
  const uint32_t row = (uint32_t)(w > h ? w : h) * s.bpp;
  const uint32_t cap = flush_max < CONFIG_SYS_LVGL_BUFFER_MAX ? flush_max : CONFIG_SYS_LVGL_BUFFER_MAX;
  ERR_CHECK(cap >= row, ERR_LVGL_FLUSH_MAX, .id = id, .flush_max = flush_max, .row = row);
  uint32_t size = (uint32_t)w * h * s.bpp / 10u;
  size = size < row ? row : size > cap ? cap : size;
  *out = size - size % row;
  return NULL;
}

static err_h install_steps(const sys_lvgl_install_t* in) {
  display_geometry_t g;
  uint8_t orientation = 0;
  err_h err = sys_display_get_geometry(in->display, &g);
  if (err == NULL) {
    err = sys_display_get_orientation(in->display, &orientation);
  }
  if (err != NULL) {
    return ERR_WRAP(err, ERR_LVGL_NOT_DISPLAY, .id = in->display);
  }
  lv_color_format_t cf;
  if (g.format == SYS_DISPLAY_FORMAT_RGB565_SWAPPED) {
    cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
    s.bpp = 2;
  } else if (g.format == SYS_DISPLAY_FORMAT_RGB888) {
    cf = LV_COLOR_FORMAT_RGB888;
    s.bpp = 3;
  } else {
    return ERR_NEW(ERR_LVGL_FORMAT, .id = in->display, .format = g.format);
  }
  if (in->touch != SYS_DEVICE_ID_NONE) {
    touch_point_t p;
    err = sys_touch_get_point(in->touch, &p);
    if (err != NULL) {
      return ERR_WRAP(err, ERR_LVGL_NOT_TOUCH, .id = in->touch);
    }
  }
  s.display = in->display;
  s.touch = in->touch;
  s.rotation = orientation;
  s.health_at = xTaskGetTickCount() + MSEC(CONFIG_SYS_LVGL_HEALTH_MS);

  // The lv_display is in the panel's native size; its rotation is the panel's orientation.
  const bool turned = orientation == SYS_DISPLAY_ORIENTATION_90 || orientation == SYS_DISPLAY_ORIENTATION_270;
  const uint16_t w = turned ? g.height : g.width;
  const uint16_t h = turned ? g.width : g.height;
  ERR_TRY(buffer_size(in->display, w, h, g.flush_max, &s.buffer_bytes));

  // LVGL on its one pool: every object, style and cache of the screen is in it, nothing in the system heap.
  s.heap_bytes = in->heap_bytes != 0u ? in->heap_bytes : CONFIG_SYS_LVGL_HEAP_DEFAULT;
  ERR_CHECK(s.heap_bytes >= SYS_LVGL_HEAP_MIN && s.heap_bytes <= SYS_LVGL_HEAP_MAX, ERR_LVGL_HEAP,
            .heap_bytes = s.heap_bytes, .min = SYS_LVGL_HEAP_MIN, .max = SYS_LVGL_HEAP_MAX);
  s_pool = pool_alloc(s.heap_bytes);
  ERR_CHECK(s_pool != NULL, ERR_LVGL_NO_MEMORY, .bytes = s.heap_bytes);
  lv_init();
  s.lv_up = true;
  if (s.heap_bytes > LV_MEM_SIZE) {
    ERR_CHECK(lv_mem_add_pool((uint8_t*)s_pool + LV_MEM_SIZE, s.heap_bytes - LV_MEM_SIZE) != NULL, ERR_LVGL_NO_MEMORY,
              .bytes = s.heap_bytes);
  }
  lv_tick_set_cb(tick_cb);
  lv_log_register_print_cb(log_cb);

  s.buf[0] = buffer_alloc(s.buffer_bytes);
  ERR_CHECK(s.buf[0] != NULL, ERR_LVGL_NO_MEMORY, .bytes = s.buffer_bytes);
  s.buf[1] = buffer_alloc(s.buffer_bytes);
  s.buffers = s.buf[1] != NULL ? 2 : 1;

  s.disp = lv_display_create(w, h);
  ERR_CHECK(s.disp != NULL, ERR_LVGL_NO_MEMORY, .bytes = 0);
  lv_display_set_color_format(s.disp, cf);
  lv_display_set_buffers(s.disp, s.buf[0], s.buf[1], s.buffer_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(s.disp, flush_cb);
  lv_display_set_flush_wait_cb(s.disp, flush_wait_cb);
  lv_display_set_rotation(s.disp, (lv_display_rotation_t)orientation);

  if (s.touch != SYS_DEVICE_ID_NONE) {
    s.indev = lv_indev_create();
    ERR_CHECK(s.indev != NULL, ERR_LVGL_NO_MEMORY, .bytes = 0);
    lv_indev_set_type(s.indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s.indev, touch_read_cb);
    lv_indev_set_display(s.indev, s.disp);
  }
  return NULL;
}

/* ---- C API ---- */

err_h sys_lvgl_init(void) {
  s.display = SYS_DEVICE_ID_NONE;
  s.touch = SYS_DEVICE_ID_NONE;
  U_TASK_START(s_lvgl_task, lvgl_task, NULL, CONFIG_SYS_LVGL_TASK_PRIO);
  ERR_CHECK(s_lvgl_task != NULL, ERR_LVGL_NO_MEMORY, .bytes = CONFIG_SYS_LVGL_TASK_STACK);
  return NULL;
}

void sys_lvgl_lock(void) {
  (void)xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
}

void sys_lvgl_unlock(void) {
  (void)xSemaphoreGiveRecursive(s_lock);
}

err_h sys_lvgl_install(const sys_lvgl_install_t* install) {
  ERR_CHECK(install != NULL, ERR_DEV_BAD_ARG);
  sys_lvgl_lock();
  err_h err = s.installed ? ERR_NEW(ERR_LVGL_INSTALLED) : install_steps(install);
  if (err != NULL) {
    if (!s.installed) {
      teardown();
    }
  } else {
    s.installed = true;
    __atomic_store_n(&s_display_id, s.display, __ATOMIC_RELAXED);
    apply_drawing();
    post(0u);
  }
  sys_lvgl_unlock();
  return err;
}

err_h sys_lvgl_get_install(sys_lvgl_install_t* out) {
  ERR_CHECK(out != NULL, ERR_DEV_BAD_ARG);
  sys_lvgl_lock();
  *out = (sys_lvgl_install_t){.display = s.display, .touch = s.touch, .heap_bytes = s.installed ? s.heap_bytes : 0u};
  sys_lvgl_unlock();
  return NULL;
}

err_h sys_lvgl_reset(void) {
  sys_lvgl_lock();
  if (s.installed) {
    wait_flush_end();
    if (s.reset_hook != NULL) {
      s.reset_hook();
    }
    teardown();
  }
  sys_lvgl_unlock();
  return NULL;
}

err_h sys_lvgl_start(void) {
  sys_lvgl_lock();
  const bool installed = s.installed;
  if (installed) {
    s.stopped = false;
    apply_drawing();
    invalidate();
    post(0u);
  }
  sys_lvgl_unlock();
  ERR_CHECK(installed, ERR_LVGL_NOT_INSTALLED);
  return NULL;
}

err_h sys_lvgl_stop(void) {
  sys_lvgl_lock();
  const bool installed = s.installed;
  if (installed) {
    wait_flush_end();
    s.stopped = true;
    apply_drawing();
  }
  sys_lvgl_unlock();
  ERR_CHECK(installed, ERR_LVGL_NOT_INSTALLED);
  return NULL;
}

err_h sys_lvgl_set_rotation(uint8_t rotation) {
  sys_lvgl_lock();
  err_h err = NULL;
  if (!s.installed) {
    err = ERR_NEW(ERR_LVGL_NOT_INSTALLED);
  } else {
    wait_flush_end();
    err = sys_display_set_orientation(s.display, rotation);
    if (err == NULL) {
      s.rotation = rotation;
      lv_display_set_rotation(s.disp, (lv_display_rotation_t)rotation);
    }
  }
  sys_lvgl_unlock();
  return err;
}

err_h sys_lvgl_get_rotation(uint8_t* out) {
  ERR_CHECK(out != NULL, ERR_DEV_BAD_ARG);
  sys_lvgl_lock();
  const bool installed = s.installed;
  *out = s.rotation;
  sys_lvgl_unlock();
  ERR_CHECK(installed, ERR_LVGL_NOT_INSTALLED);
  return NULL;
}

err_h sys_lvgl_get_status(sys_lvgl_status_t* out) {
  ERR_CHECK(out != NULL, ERR_DEV_BAD_ARG);
  sys_lvgl_lock();
  sys_lvgl_state_e state = SYS_LVGL_STATE_EMPTY;
  if (s.installed) {
    state = s.stopped ? SYS_LVGL_STATE_STOPPED : s.paused ? SYS_LVGL_STATE_PAUSED : SYS_LVGL_STATE_RUNNING;
  }
  lv_mem_monitor_t mon = {0};
  if (s.lv_up) {
    lv_mem_monitor(&mon);
  }
  *out = (sys_lvgl_status_t){
      .state = (uint8_t)state,
      .rotation = s.rotation,
      .buffers = s.buffers,
      .width = s.disp != NULL ? (uint16_t)lv_display_get_horizontal_resolution(s.disp) : 0u,
      .height = s.disp != NULL ? (uint16_t)lv_display_get_vertical_resolution(s.disp) : 0u,
      .buffer_bytes = s.buffer_bytes,
      .heap_budget = (uint32_t)mon.total_size,
      .heap_used = (uint32_t)(mon.total_size - mon.free_size),
      .heap_peak = (uint32_t)mon.max_used,
      .flushes = s.flushes,
      .flush_errors = s.flush_errors,
  };
  sys_lvgl_unlock();
  return NULL;
}

err_h sys_lvgl_pool_check(uint32_t need) {
  sys_lvgl_lock();
  const bool up = s.lv_up;
  lv_mem_monitor_t mon = {0};
  if (up) {
    lv_mem_monitor(&mon);
  }
  sys_lvgl_unlock();
  ERR_CHECK(up, ERR_LVGL_NOT_INSTALLED);
  const uint32_t free_bytes = (uint32_t)mon.free_biggest_size;
  ERR_CHECK(free_bytes >= need && free_bytes - need >= CONFIG_SYS_LVGL_POOL_RESERVE, ERR_LVGL_POOL_FULL,
            .free = free_bytes, .need = need, .reserve = CONFIG_SYS_LVGL_POOL_RESERVE);
  return NULL;
}

void sys_lvgl_set_reset_hook(sys_lvgl_reset_f fn) {
  s.reset_hook = fn;
}

void sys_lvgl_device_state(uint16_t id, sys_device_state_e change) {
  (void)change;
  const uint16_t display = __atomic_load_n(&s_display_id, __ATOMIC_RELAXED);
  if (display != SYS_DEVICE_ID_NONE && (id == display || id == SYS_DEVICE_ID_NONE)) {
    post(BIT_DISPLAY_STATE);
  }
}
