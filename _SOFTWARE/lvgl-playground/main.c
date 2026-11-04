/* LVGL playground: LVGL on a PC against a mocked panel and a mocked touch, no board and no OS.
 * Everything LVGL asks of the platform is in the "platform glue" block: that is the list of
 * things a real port has to provide. The scenario below runs on a virtual clock (no sleeping)
 * and saves the panel as BMP files. */
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "mock_panel.h"
#include "mock_touch.h"
#ifdef HAVE_WINDOW
#include "mock_window.h"
#endif

#define SCREEN_W 240
#define SCREEN_H 320

/* ---------- platform glue: what LVGL needs from the platform ---------- */

static uint32_t virtual_ms;   /* the scenario's clock; the window uses real time instead */
static bool realtime;

/* 1. A millisecond tick. */
static uint32_t tick_cb(void)
{
#ifdef HAVE_WINDOW
    if (realtime) return mock_window_ticks();
#endif
    return virtual_ms;
}

/* 2. Draw buffers: a tenth of the screen, two of them (one is sent while the other is rendered). */
#define BUF_BYTES (SCREEN_W * SCREEN_H / 10 * sizeof(lv_color16_t))
static LV_ATTRIBUTE_MEM_ALIGN uint8_t buf1[BUF_BYTES];
static LV_ATTRIBUTE_MEM_ALIGN uint8_t buf2[BUF_BYTES];

/* 3. flush: LVGL hands over a rendered rectangle, the port sends it to the panel and says it is done. */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    mock_panel_set_window(area->x1, area->y1, area->x2, area->y2);
    mock_panel_write(px_map, lv_area_get_size(area) * sizeof(lv_color16_t));
    lv_display_flush_ready(disp);   /* on real hardware: from the DMA-done interrupt */
}

/* 4. Input: LVGL polls the touch controller. */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    int x, y;
    (void)indev;
    data->state = mock_touch_get(&x, &y) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = x;
    data->point.y = y;
}

static lv_display_t *platform_init(void)
{
    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, buf1, buf2, sizeof buf1, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);

    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read_cb);
    return disp;
}

/* 5. The loop: lv_timer_handler() every few ms from one task. */
static void run(uint32_t ms)
{
    while (ms) {
        uint32_t step = ms < 5 ? ms : 5;
        virtual_ms += step;
        ms -= step;
        lv_timer_handler();
    }
}

/* ---------- the UI under test ---------- */

static lv_obj_t *btn, *btn_label, *slider, *bar, *value_label;
static int clicks;

static void btn_cb(lv_event_t *e)
{
    (void)e;
    lv_label_set_text_fmt(btn_label, "Clicked %d", ++clicks);
}

static void slider_cb(lv_event_t *e)
{
    int32_t v = lv_slider_get_value(lv_event_get_target_obj(e));
    lv_bar_set_value(bar, v, LV_ANIM_OFF);
    lv_label_set_text_fmt(value_label, "%d %%", (int)v);
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "LVGL playground");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    btn = lv_button_create(scr);
    lv_obj_set_size(btn, 160, 48);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, NULL);
    btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Click me");
    lv_obj_center(btn_label);

    slider = lv_slider_create(scr);
    lv_obj_set_width(slider, 180);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 180, 16);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 200);

    value_label = lv_label_create(scr);
    lv_label_set_text(value_label, "0 %");
    lv_obj_align(value_label, LV_ALIGN_TOP_MID, 0, 230);
}

/* ---------- the window: the same UI in real time, the mouse is the finger ---------- */

#ifdef HAVE_WINDOW
static int run_window(int scale)
{
    if (mock_window_open("LVGL playground", SCREEN_W, SCREEN_H, scale)) {
        fprintf(stderr, "cannot open the window\n");
        return 1;
    }
    uint32_t seen = 0, last_ms = mock_window_ticks(), last_windows = 0, last_bytes = 0;
    mock_window_present();
    while (mock_window_poll()) {
        lv_timer_handler();
        const mock_panel_stats_t *st = mock_panel_stats();
        if (st->windows != seen) {   /* something was flushed to the panel */
            seen = st->windows;
            mock_window_present();
        }
        uint32_t now = mock_window_ticks();
        if (now - last_ms >= 1000) {
            char title[96];
            snprintf(title, sizeof title, "LVGL playground | %u flushes/s, %u kB/s", (unsigned)(st->windows - last_windows),
                     (unsigned)((st->bus_bytes - last_bytes) / 1024));
            mock_window_set_title(title);
            last_ms = now;
            last_windows = st->windows;
            last_bytes = st->bus_bytes;
        }
        mock_window_delay(5);
    }
    mock_window_close();
    return 0;
}
#endif

/* ---------- scenario ---------- */

static const char *out_dir = ".";

static void snapshot(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.bmp", out_dir, name);
    printf("%-14s t=%4ums  %s\n", name, (unsigned)virtual_ms, mock_panel_save_bmp(path) ? "FAILED" : path);
}

static void tap(lv_obj_t *obj)
{
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    mock_touch_set((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2, true);
    run(60);
    mock_touch_set((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2, false);
    run(60);
}

int main(int argc, char **argv)
{
    int window_scale = 0;
    if (argc > 1 && !strcmp(argv[1], "--window")) window_scale = argc > 2 ? atoi(argv[2]) : 2;
    else if (argc > 1) out_dir = argv[1];
#ifndef HAVE_WINDOW
    if (window_scale) {
        fprintf(stderr, "built without PLAYGROUND_WINDOW\n");
        return 1;
    }
#endif
    if (mock_panel_init(SCREEN_W, SCREEN_H)) return 1;

    realtime = window_scale != 0;
    platform_init();
    build_ui();

#ifdef HAVE_WINDOW
    if (window_scale) {
        int rc = run_window(window_scale);
        mock_panel_deinit();
        return rc;
    }
#endif

    run(100);
    snapshot("0_boot");

    tap(btn);
    tap(btn);
    snapshot("1_clicked");

    lv_slider_set_value(slider, 70, LV_ANIM_OFF);
    lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, NULL);
    run(300);
    snapshot("2_slider");

    const mock_panel_stats_t *st = mock_panel_stats();
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    printf("\nflushes %u, pixels %u (%u bytes over the bus), full screen = %u px\n", (unsigned)st->windows,
           (unsigned)st->pixels, (unsigned)st->bus_bytes, SCREEN_W * SCREEN_H);
    printf("LVGL heap: %u of %u bytes used at peak, draw buffers 2 x %u bytes\n", (unsigned)mon.max_used,
           (unsigned)mon.total_size, (unsigned)BUF_BYTES);

    mock_panel_deinit();
    return 0;
}
