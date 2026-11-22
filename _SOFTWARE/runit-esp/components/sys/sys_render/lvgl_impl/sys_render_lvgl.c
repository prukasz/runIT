/**
 * @file
 * @brief The render engine on LVGL 9.3: sys_render_driver, the only place that includes lvgl.h.
 *        One display at a time (sys_render has one). Not tested in TDD.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "sys_render/sys_render_interface.h"
#include "sys_render_lvgl_hooks.h"

/** @brief The pool given by set_pool, until the next one */
static void *s_pool;
/** @brief Its size */
static uint32_t s_pool_bytes;
/** @brief What display_create was given: flush, wait and their ctx */
static sys_render_display_cfg_t s_cfg;
/** @brief Bytes of a pixel of the display's format */
static uint32_t s_bpp;

void *sys_render_lvgl_pool_take(size_t size)
{
    (void)size;
    return s_pool;
}

void sys_render_lvgl_assert_failed(void)
{
    fprintf(stderr, "sys_render: LVGL assert\n");
    abort();
}

static void log_cb(lv_log_level_t level, const char *text)
{
    (void)level;
    fputs(text, stderr);
}

/* ---- the engine's display callbacks: through to sys_render's flush and wait ---- */

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (s_cfg.flush == NULL) {
        lv_display_flush_ready(disp);
        return;
    }
    const display_window_t w = {.x1 = (uint16_t)area->x1,
                                .y1 = (uint16_t)area->y1,
                                .x2 = (uint16_t)area->x2,
                                .y2 = (uint16_t)area->y2};
    const uint32_t len = (uint32_t)lv_area_get_size(area) * s_bpp;
    s_cfg.flush(s_cfg.ctx, &w, px_map, len);
}

static void flush_wait_cb(lv_display_t *disp)
{
    (void)disp;
    if (s_cfg.wait != NULL) {
        s_cfg.wait(s_cfg.ctx); /* it gives display_flush_ready */
    }
}

/* ---- sys_render_driver_t ---- */

static void drv_set_pool(void *pool, uint32_t pool_bytes)
{
    s_pool = pool;
    s_pool_bytes = pool_bytes;
}

static bool drv_init(uint32_t (*tick_ms)(void))
{
    if (s_pool == NULL || s_pool_bytes < LV_MEM_SIZE ||
        s_pool_bytes > LV_MEM_SIZE + LV_MEM_POOL_EXPAND_SIZE) {
        return false;
    }
    lv_init(); /* its heap on the first LV_MEM_SIZE bytes (LV_MEM_POOL_ALLOC) */
    if (s_pool_bytes > LV_MEM_SIZE &&
        lv_mem_add_pool((uint8_t *)s_pool + LV_MEM_SIZE, s_pool_bytes - LV_MEM_SIZE) == NULL) {
        lv_deinit();
        return false;
    }
    lv_tick_set_cb(tick_ms);
    lv_log_register_print_cb(log_cb);
    return true;
}

static void drv_deinit(void)
{
    lv_deinit(); /* whatever LVGL still holds is in the pool, freed with it */
    s_pool = NULL;
    s_pool_bytes = 0;
}

static uint32_t drv_timer_handler(void)
{
    return lv_timer_handler();
}

static void *drv_display_create(const sys_render_display_cfg_t *cfg)
{
    lv_display_t *disp = lv_display_create(cfg->w, cfg->h);
    if (disp == NULL) {
        return NULL;
    }
    s_cfg = *cfg;
    const bool rgb888 = (cfg->format == SYS_DISPLAY_FORMAT_RGB888);
    s_bpp = rgb888 ? 3u : 2u;
    lv_display_set_color_format(disp,
                                rgb888 ? LV_COLOR_FORMAT_RGB888 : LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_buffers(disp, cfg->buf1, cfg->buf2, cfg->buf_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_flush_wait_cb(disp, flush_wait_cb);
    lv_timer_set_period(lv_display_get_refr_timer(disp), cfg->refresh_ms);
    return disp;
}

static void drv_display_delete(void *disp)
{
    lv_display_delete((lv_display_t *)disp);
    memset(&s_cfg, 0, sizeof(s_cfg));
}

static void drv_display_set_rotation(void *disp, uint8_t rotation)
{
    /* sys_display_orientation_e 0..3 is lv_display_rotation_t 0..3: the panel turns the picture
       (its scan direction), LVGL only lays out in the turned size */
    lv_display_set_rotation((lv_display_t *)disp, (lv_display_rotation_t)rotation);
}

static void drv_display_flush_ready(void *disp)
{
    lv_display_flush_ready((lv_display_t *)disp);
}

static void drv_invalidate_screen(void)
{
    lv_obj_t *screen = lv_screen_active();
    if (screen != NULL) {
        lv_obj_invalidate(screen);
    }
}

static uint32_t drv_pool_free_biggest(void)
{
    lv_mem_monitor_t mon = {0};
    lv_mem_monitor(&mon);
    return (uint32_t)mon.free_biggest_size;
}

const sys_render_driver_t sys_render_driver = {
    .set_pool = drv_set_pool,
    .init = drv_init,
    .deinit = drv_deinit,
    .timer_handler = drv_timer_handler,
    .display_create = drv_display_create,
    .display_delete = drv_display_delete,
    .display_set_rotation = drv_display_set_rotation,
    .display_flush_ready = drv_display_flush_ready,
    .invalidate_screen = drv_invalidate_screen,
    .pool_free_biggest = drv_pool_free_biggest,
};
