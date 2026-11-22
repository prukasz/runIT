/**
 * @file
 * @brief The render adapter (sys_render): the state of one installed display, the checks of the
 *        arguments of install and set_rotation, and the acquiring and releasing of what the engine
 *        runs on (its pool, the draw buffers, its display). The engine itself (sys_render_driver)
 *        and the memory (board_mem_*) are reached only through their interfaces.
 */
#include "FreeRTOS.h"
#include "semphr.h"

#include "board_time.h"
#include "sys_device.h"
#include "sys_device_events.h"
#include "sys_render/config/sys_render_config.h"
#include "sys_render/internal/sys_render_internal.h"
#include "sys_render/sys_render.h"
#include "sys_render/sys_render_buff_interface.h"
#include "sys_render/sys_render_interface.h"
#include "utils.h"

ERR_MODULE_DEFINE(MODULE_ID_RENDER, ERR_SYS_RENDER_MAP)

/** @brief Bytes of a pixel of the only format drawn (SYS_DISPLAY_FORMAT_RGB565_SWAPPED) */
#define BYTES_PER_PIXEL 2u

/** @brief The panel as install reads it: native size, the draw buffer and the first rotation */
typedef struct {
    uint16_t width;      /**< Native width of the panel (orientation 0), px */
    uint16_t height;     /**< Native height of the panel (orientation 0), px */
    uint32_t buf_bytes;  /**< Size of one draw buffer */
    uint8_t format;      /**< sys_display_format_e */
    uint8_t orientation; /**< sys_display_orientation_e the panel is in */
} panel_t;

/** @brief How many events of the display's life the adapter listens to */
#define SUB_COUNT 4u

/** @brief What the listener flagged for the render task: the display was suspended or resumed */
#define BIT_STATE 0x01u
/** @brief What the listener flagged for the render task: the display was reset */
#define BIT_RESET 0x02u
/** @brief What the listener flagged for the render task: the display was removed */
#define BIT_GONE 0x04u

/** @brief The one instance of the adapter */
typedef struct {
    sys_render_state_e state;        /**< EMPTY until install worked */
    sys_render_install_t install;    /**< What install was given */
    uint8_t rotation;                /**< sys_display_orientation_e */
    bool engine_up;                  /**< The engine's init worked: deinit is due */
    void *pool;                      /**< The engine's pool */
    void *buf[SYS_RENDER_BUF_COUNT]; /**< The draw buffers */
    void *display;                   /**< The engine's display */
    uint16_t sub[SUB_COUNT];         /**< Handles of the subscriptions */
    uint8_t subs;                    /**< How many subscriptions were made */
    bool flush_pending;              /**< A flush was accepted and its end not yet taken */
    bool refusal_reported;           /**< A refusal was reported: the next ones are not, until one
                                          goes through */
    bool redraw; /**< A flush was lost: the whole screen again after the round */
} render_t;

static render_t s;

/** @brief What the listener flagged, taken by the render task (BIT_*) */
static uint8_t s_bits;

static const uint32_t k_device_events[SUB_COUNT] = {SYS_DEVICE_EV_SUSPENDED, SYS_DEVICE_EV_RESUMED,
                                                    SYS_DEVICE_EV_RESET, SYS_DEVICE_EV_REMOVED};

/** @brief The state last published as RENDER_EV_STATE_CHANGED (EMPTY at boot) */
static sys_render_state_e s_state_pub = SYS_RENDER_STATE_EMPTY;

U_RECURSIVE_MUTEX_DEFINE(s_lock);
/* The end of the flush in flight: given by the display's done (ISR), taken by wait */
U_BINARY_SEM_DEFINE(s_flush_done);
/* The render task (not in TDD): started by the first install, runs sys_render_task_step */
U_TASK_DEFINE(s_task, SYS_RENDER_TASK_STACK);

static void finish_flush(bool reset_on_timeout);
static void start_task(void);

/** @brief Wakes the render task (from a task, not an ISR): bits to handle or drawing to start */
static void wake_task(void)
{
    if (s_task != NULL) {
        (void)xTaskNotifyGive(s_task);
    }
}

static uint32_t tick_ms(void)
{
    return (uint32_t)board_time_uptime_ms();
}

/** @brief Frees all that install made and brings the adapter back to EMPTY */
static void release(void)
{
    finish_flush(false); /* the buffers are freed below: no transfer may still read them */
    if (s.display != NULL) {
        sys_render_driver.display_delete(s.display);
    }
    if (s.engine_up) {
        sys_render_driver.deinit();
    }
    for (uint32_t i = 0; i < SYS_RENDER_BUF_COUNT; i++) {
        board_mem_free(s.buf[i]);
    }
    board_mem_free(s.pool);
    for (uint8_t i = 0; i < s.subs; i++) {
        err_free(sys_event_unsubscribe(s.sub[i]));
    }
    memset(&s, 0, sizeof(s));
    s.state = SYS_RENDER_STATE_EMPTY;
    __atomic_store_n(&s_bits, 0, __ATOMIC_RELAXED);
}

/** @brief Takes the state; a state other than the last published is published (deferred) */
static void set_state(sys_render_state_e state)
{
    s.state = state;
    if (state != s_state_pub) {
        s_state_pub = state;
        err_report(SYS_EVENT_PUBLISH_DEFER(RENDER_EV_STATE_CHANGED, 0, 0, .state = (uint8_t)state));
    }
    if (state == SYS_RENDER_STATE_RUNNING) {
        wake_task();
    }
}

static err_h check_install_args(const sys_render_install_t *in)
{
    ERR_CHECK_IN_RANGE(in->refresh_hz, SYS_RENDER_REFRESH_HZ_MIN, SYS_RENDER_REFRESH_HZ_MAX,
                       ERR_RENDER_REFRESH_HZ_OUT_OF_RANGE, .refresh_hz = in->refresh_hz,
                       .min = SYS_RENDER_REFRESH_HZ_MIN, .max = SYS_RENDER_REFRESH_HZ_MAX);
    ERR_CHECK_IN_RANGE(in->heap_bytes, SYS_RENDER_HEAP_BYTES_MIN, SYS_RENDER_HEAP_BYTES_MAX,
                       ERR_RENDER_HEAP_BYTES_OUT_OF_RANGE, .heap_bytes = in->heap_bytes,
                       .min = SYS_RENDER_HEAP_BYTES_MIN, .max = SYS_RENDER_HEAP_BYTES_MAX);
    return NULL;
}

/** @brief Reads GEOMETRY and ORIENTATION of the display and sizes the draw buffer */
static err_h read_panel(uint8_t id, panel_t *panel)
{
    display_geometry_t geometry;
    uint8_t orientation = 0;

    err_h e = SYS_DISPLAY_READ(id, OP_GEOMETRY, &geometry);
    if (e != NULL) {
        return e;
    }
    e = sys_display_get_orientation(id, &orientation);
    if (e != NULL) {
        return e;
    }
    ERR_CHECK(geometry.format == SYS_DISPLAY_FORMAT_RGB565_SWAPPED,
              ERR_RENDER_PIXEL_FORMAT_UNSUPPORTED, .id = id, .format = geometry.format);

    /* GEOMETRY is in the current orientation: at 90 and 270 deg its sides are the native ones
       swapped */
    const bool turned =
        (orientation == SYS_DISPLAY_ORIENTATION_90) || (orientation == SYS_DISPLAY_ORIENTATION_270);
    panel->width = turned ? geometry.height : geometry.width;
    panel->height = turned ? geometry.width : geometry.height;
    panel->format = geometry.format;
    panel->orientation = orientation;

    const uint32_t row = (uint32_t)panel->width * BYTES_PER_PIXEL;
    ERR_CHECK(geometry.flush_max >= row, ERR_RENDER_ROW_EXCEEDS_FLUSH_MAX, .id = id,
              .flush_max = geometry.flush_max, .row = row);
    uint32_t lines = geometry.flush_max / row;
    lines = lines < SYS_RENDER_BUF_LINES ? lines : SYS_RENDER_BUF_LINES;
    panel->buf_bytes = row * lines;
    return NULL;
}

/** @brief ERR_RENDER_ALLOC_FAILED for `bytes` of memory of `caps`, with what is left of it */
static err_h alloc_failed(uint32_t bytes, uint32_t caps)
{
    return ERR_NEW(ERR_RENDER_ALLOC_FAILED, .bytes = bytes, .free = board_mem_largest_free(caps));
}

/* ---- flush (not in TDD: DESIGN §5 "Flush") ---- */

/** @brief The display's done, from the transfer-done ISR: only gives the semaphore */
static void on_flush_done(void *ctx)
{
    (void)ctx;
    BaseType_t woken = pdFALSE;
    (void)xSemaphoreGiveFromISR(s_flush_done, &woken);
    portYIELD_FROM_ISR(woken);
}

/** @brief The engine's flush: hands the window to the display; a refusal ends the flush now */
static void on_flush(void *ctx, const display_window_t *w, const void *px, uint32_t len)
{
    (void)ctx;
    err_h e = sys_display_flush(s.install.display, px, len, w, on_flush_done, NULL);
    s.flush_pending = (e == NULL);
    if (e == NULL) {
        s.refusal_reported = false;
        return;
    }
    s.redraw = true;
    if (s.refusal_reported) {
        err_free(e);
    } else {
        s.refusal_reported = true;
        err_report(e);
    }
}

/**
 * @brief Waits for the end of the flush in flight, if any. On its timeout: reported, and with
 * reset_on_timeout the display is reset (its RESET event turns the panel back and redraws)
 */
static void finish_flush(bool reset_on_timeout)
{
    if (!s.flush_pending) {
        return;
    }
    s.flush_pending = false;
    if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(SYS_RENDER_FLUSH_TIMEOUT_MS)) == pdTRUE) {
        return;
    }
    err_report(ERR_NEW(ERR_DISPLAY_FLUSH_TIMEOUT, .id = s.install.display,
                       .timeout_ms = SYS_RENDER_FLUSH_TIMEOUT_MS));
    if (reset_on_timeout) {
        err_report(SYS_DEVICE_SET(s.install.display, CONTRACT_SYS_DEVICE, OP_RESET));
        s.redraw = true;
    }
}

/** @brief The engine's wait: the end of the flush (or its timeout), then the engine goes on */
static void on_flush_wait(void *ctx)
{
    (void)ctx;
    finish_flush(true);
    sys_render_driver.display_flush_ready(s.display);
}

/** @brief Takes the pool and the draw buffers, starts the engine and makes its display */
static err_h acquire(const sys_render_install_t *in, const panel_t *panel)
{
    s.pool = board_mem_alloc(in->heap_bytes, BOARD_MEM_SPIRAM);
    if (s.pool == NULL) {
        return alloc_failed(in->heap_bytes, BOARD_MEM_SPIRAM);
    }
    sys_render_driver.set_pool(s.pool, in->heap_bytes);
    if (!sys_render_driver.init(tick_ms)) {
        return alloc_failed(in->heap_bytes, BOARD_MEM_SPIRAM);
    }
    s.engine_up = true;

    for (uint32_t i = 0; i < SYS_RENDER_BUF_COUNT; i++) {
        s.buf[i] = board_mem_alloc(panel->buf_bytes, BOARD_MEM_SPIRAM | BOARD_MEM_DMA);
        if (s.buf[i] == NULL) {
            return alloc_failed(panel->buf_bytes, BOARD_MEM_SPIRAM | BOARD_MEM_DMA);
        }
    }

    /* nothing is drawn before start: the engine's round runs only in RUNNING */
    (void)xSemaphoreTake(s_flush_done, 0); /* a done left from before */
    const sys_render_display_cfg_t cfg = {.w = panel->width,
                                          .h = panel->height,
                                          .format = panel->format,
                                          .buf1 = s.buf[0],
                                          .buf2 = s.buf[1],
                                          .buf_bytes = panel->buf_bytes,
                                          .refresh_ms = 1000u / in->refresh_hz,
                                          .flush = on_flush,
                                          .wait = on_flush_wait,
                                          .ctx = NULL};
    s.display = sys_render_driver.display_create(&cfg);
    if (s.display == NULL) {
        /* the engine does not say how much its display needed: bytes 0 */
        return ERR_NEW(ERR_RENDER_ALLOC_FAILED, .bytes = 0,
                       .free = sys_render_driver.pool_free_biggest());
    }
    return NULL;
}

/** @brief Flags what happened to the display; the render task does the rest */
static err_h on_device_event(const sys_event_t *event, void *ctx)
{
    (void)ctx;
    if (event->key.source != SYS_DEVICE_ID_NONE && event->key.source != s.install.display) {
        return NULL;
    }
    uint8_t bit = BIT_STATE;
    if (event->key.tag == SYS_DEVICE_EV_RESET) {
        bit = BIT_RESET;
    } else if (event->key.tag == SYS_DEVICE_EV_REMOVED) {
        bit = BIT_GONE;
    }
    __atomic_fetch_or(&s_bits, bit, __ATOMIC_RELAXED);
    wake_task();
    return NULL;
}

static err_h subscribe_events(void)
{
    for (uint8_t i = 0; i < SUB_COUNT; i++) {
        const sys_event_key_t key = {.tag = k_device_events[i],
                                     .source = SYS_EVENT_ANY_SOURCE,
                                     .channel = SYS_EVENT_ANY_CHANNEL};
        ERR_TRY(sys_event_subscribe(key, on_device_event, NULL, SYS_EVENT_RUN_INLINE, &s.sub[i]));
        s.subs++;
    }
    return NULL;
}

/** @brief Puts the panel in the rotation the adapter keeps */
static void turn_panel(void)
{
    err_report(sys_display_set_orientation(s.install.display, s.rotation));
}

/** @brief SUSPENDED or RESUMED: the state follows the display's INFO */
static void handle_state_bit(void)
{
    sys_device_info_t info;
    err_h e = SYS_DEVICE_GET(s.install.display, CONTRACT_SYS_DEVICE, OP_INFO, &info);
    if (e != NULL) {
        err_report(e);
        return;
    }
    if (info.suspended != 0) {
        if (s.state == SYS_RENDER_STATE_RUNNING) {
            set_state(SYS_RENDER_STATE_PAUSED);
        }
    } else if (s.state == SYS_RENDER_STATE_PAUSED) {
        turn_panel();
        sys_render_driver.invalidate_screen();
        set_state(SYS_RENDER_STATE_RUNNING);
    }
}

/** @brief The display was reset: its rotation again, and a redraw while drawing */
static void handle_reset_bit(void)
{
    if (s.state == SYS_RENDER_STATE_PAUSED) {
        return;
    }
    turn_panel();
    if (s.state == SYS_RENDER_STATE_RUNNING) {
        sys_render_driver.invalidate_screen();
    }
}

/** @brief Announces the reset, frees all and brings the adapter back to EMPTY (not EMPTY now) */
static void drop_adapter(void)
{
    err_report(SYS_EVENT_PUBLISH(RENDER_EV_RESET, 0, 0));
    release();
    set_state(SYS_RENDER_STATE_EMPTY);
}

void sys_render_lock(void)
{
    (void)xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
}

void sys_render_unlock(void)
{
    (void)xSemaphoreGiveRecursive(s_lock);
}

err_h sys_render_install(const sys_render_install_t *install)
{
    ERR_CHECK(install != NULL, ERR_NULL_PTR);
    U_RMUTEX_GUARD(s_lock);
    ERR_CHECK(s.state == SYS_RENDER_STATE_EMPTY, ERR_RENDER_ALREADY_INSTALLED);
    ERR_TRY(check_install_args(install));

    panel_t panel = {0};
    ERR_TRY(read_panel(install->display, &panel));
    s.install = *install;
    err_h e = acquire(install, &panel);
    if (e == NULL) {
        e = subscribe_events();
    }
    if (e != NULL) {
        release();
        return e;
    }
    sys_render_driver.display_set_rotation(s.display, panel.orientation);
    s.rotation = panel.orientation;
    set_state(SYS_RENDER_STATE_STOPPED);
    start_task();
    return NULL;
}

err_h sys_render_get_install(sys_render_install_t *out)
{
    ERR_CHECK(out != NULL, ERR_NULL_PTR);
    U_RMUTEX_GUARD(s_lock);
    if (s.state == SYS_RENDER_STATE_EMPTY) {
        memset(out, 0, sizeof(*out));
        out->display = SYS_DEVICE_ID_NONE;
    } else {
        *out = s.install;
    }
    return NULL;
}

err_h sys_render_start(void)
{
    U_RMUTEX_GUARD(s_lock);
    ERR_CHECK(s.state != SYS_RENDER_STATE_EMPTY, ERR_RENDER_NOT_INSTALLED);
    if (s.state != SYS_RENDER_STATE_STOPPED) {
        return NULL;
    }
    sys_device_info_t info;
    ERR_TRY(SYS_DEVICE_GET(s.install.display, CONTRACT_SYS_DEVICE, OP_INFO, &info));
    if (info.suspended != 0) {
        set_state(SYS_RENDER_STATE_PAUSED);
        return NULL;
    }
    sys_render_driver.invalidate_screen();
    set_state(SYS_RENDER_STATE_RUNNING);
    return NULL;
}

err_h sys_render_stop(void)
{
    U_RMUTEX_GUARD(s_lock);
    ERR_CHECK(s.state != SYS_RENDER_STATE_EMPTY, ERR_RENDER_NOT_INSTALLED);
    finish_flush(true); /* the last flush ends before the loop stops */
    set_state(SYS_RENDER_STATE_STOPPED);
    return NULL;
}

err_h sys_render_reset(void)
{
    U_RMUTEX_GUARD(s_lock);
    if (s.state == SYS_RENDER_STATE_EMPTY) {
        return NULL;
    }
    drop_adapter();
    return NULL;
}

err_h sys_render_set_rotation(uint8_t rotation)
{
    U_RMUTEX_GUARD(s_lock);
    ERR_CHECK(s.state != SYS_RENDER_STATE_EMPTY, ERR_RENDER_NOT_INSTALLED);
    ERR_CHECK_RANGE(rotation, SYS_DISPLAY_ORIENTATION_0, SYS_DISPLAY_ORIENTATION_270);
    finish_flush(true); /* the panel turns between flushes, never during one */
    /* a suspended panel takes the rotation when it resumes: only the engine turns now */
    if (s.state != SYS_RENDER_STATE_PAUSED) {
        ERR_TRY(sys_display_set_orientation(s.install.display, rotation));
    }
    sys_render_driver.display_set_rotation(s.display, rotation);
    s.rotation = rotation;
    err_report(SYS_EVENT_PUBLISH_DEFER(RENDER_EV_ROTATION_CHANGED, 0, 0, .rotation = rotation));
    wake_task();
    return NULL;
}

err_h sys_render_get_rotation(uint8_t *out)
{
    ERR_CHECK(out != NULL, ERR_NULL_PTR);
    U_RMUTEX_GUARD(s_lock);
    ERR_CHECK(s.state != SYS_RENDER_STATE_EMPTY, ERR_RENDER_NOT_INSTALLED);
    *out = s.rotation;
    return NULL;
}

err_h sys_render_get_state(uint8_t *out)
{
    ERR_CHECK(out != NULL, ERR_NULL_PTR);
    U_RMUTEX_GUARD(s_lock);
    *out = (uint8_t)s.state;
    return NULL;
}

err_h sys_render_pool_check(uint32_t need)
{
    U_RMUTEX_GUARD(s_lock);
    ERR_CHECK(s.state != SYS_RENDER_STATE_EMPTY, ERR_RENDER_NOT_INSTALLED);
    const uint32_t largest = sys_render_driver.pool_free_biggest();
    ERR_CHECK((uint64_t)largest >= (uint64_t)need + SYS_RENDER_POOL_RESERVE_BYTES,
              ERR_RENDER_POOL_BELOW_RESERVE, .free = largest, .need = need,
              .reserve = SYS_RENDER_POOL_RESERVE_BYTES);
    return NULL;
}

uint32_t sys_render_task_step(void)
{
    U_RMUTEX_GUARD(s_lock);
    if (s.state == SYS_RENDER_STATE_EMPTY) {
        return SYS_RENDER_TASK_SLEEP_FOREVER;
    }
    const uint8_t bits = __atomic_exchange_n(&s_bits, 0, __ATOMIC_RELAXED);
    if ((bits & BIT_GONE) != 0) {
        s.flush_pending = false; /* the removed display dropped its transfer */
        drop_adapter();
        return SYS_RENDER_TASK_SLEEP_FOREVER;
    }
    if ((bits & BIT_STATE) != 0) {
        handle_state_bit();
    }
    if ((bits & BIT_RESET) != 0) {
        handle_reset_bit();
    }
    if (s.state != SYS_RENDER_STATE_RUNNING) {
        return SYS_RENDER_TASK_SLEEP_FOREVER; /* woken by start or a display event */
    }
    uint32_t next_ms = sys_render_driver.timer_handler();
    if (s.redraw) {
        s.redraw = false;
        sys_render_driver.invalidate_screen();
        next_ms = 0;
    }
    return next_ms < SYS_RENDER_TASK_SLEEP_MAX_MS ? next_ms : SYS_RENDER_TASK_SLEEP_MAX_MS;
}

/* ---- the render task (not in TDD: DESIGN §5) ---- */

/** @brief The loop: sleeps until woken or until the engine's next round, then one step */
static void render_task(void *arg)
{
    (void)arg;
    uint32_t sleep_ms = SYS_RENDER_TASK_SLEEP_FOREVER;
    for (;;) {
        const TickType_t ticks =
            (sleep_ms == SYS_RENDER_TASK_SLEEP_FOREVER) ? portMAX_DELAY : pdMS_TO_TICKS(sleep_ms);
        (void)ulTaskNotifyTake(pdTRUE, ticks);
        sleep_ms = sys_render_task_step();
    }
}

/** @brief Starts the render task once; it lives as long as the system */
static void start_task(void)
{
    if (s_task == NULL) {
        U_TASK_START(s_task, render_task, NULL, SYS_RENDER_TASK_PRIORITY);
    }
}
