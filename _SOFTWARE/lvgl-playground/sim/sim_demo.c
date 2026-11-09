#include "sim_demo.h"

#include <stdio.h>

#include "lvgl.h"
#include "sys_lvgl.h"

static struct {
    lv_obj_t *count_label;
    lv_obj_t *bar;
    lv_obj_t *value_label;
    lv_obj_t *status_label;
    lv_timer_t *timer;
    int clicks;
} s;

static void on_click(lv_event_t *e)
{
    (void)e;
    lv_label_set_text_fmt(s.count_label, "Tapped %d", ++s.clicks);
}

static void on_slider(lv_event_t *e)
{
    const int32_t v = lv_slider_get_value(lv_event_get_target_obj(e));
    lv_bar_set_value(s.bar, v, LV_ANIM_OFF);
    lv_label_set_text_fmt(s.value_label, "%d %%", (int)v);
}

/* The adapter's own STATUS, as the app would read it (runs on the LVGL task, the lock held). */
static void on_timer(lv_timer_t *t)
{
    (void)t;
    sys_lvgl_status_t st;
    err_h err = sys_lvgl_get_status(&st);
    if (err != NULL) {
        err_free(err);
        return;
    }
    lv_label_set_text_fmt(s.status_label, "%ux%u rot %u, %u buf x %u B\nheap %u / %u B (peak %u)\nflushes %u, errors %u",
                          st.width, st.height, st.rotation * 90u, st.buffers, (unsigned)st.buffer_bytes,
                          (unsigned)st.heap_used, (unsigned)st.heap_budget, (unsigned)st.heap_peak,
                          (unsigned)st.flushes, (unsigned)st.flush_errors);
}

/* RESET deletes the screen with the widgets; the timer is not the screen's. */
static void on_reset(void)
{
    if (s.timer != NULL) {
        lv_timer_delete(s.timer);
    }
    s = (typeof(s)){0};
}

void sim_demo_init(void)
{
    sys_lvgl_set_reset_hook(on_reset);
}

int sim_demo_build(void)
{
    sys_lvgl_lock();
    sys_lvgl_status_t st;
    err_h err = sys_lvgl_get_status(&st);
    err_free(err);
    lv_display_t *disp = err == NULL && st.state != SYS_LVGL_STATE_EMPTY ? lv_display_get_default() : NULL;
    if (disp == NULL || s.timer != NULL) {
        sys_lvgl_unlock();
        return -1;
    }
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x10151C), 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr, 10, 0);
    lv_obj_set_style_pad_top(scr, 12, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "runIT screen (sim)");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8EEF5), 0);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 160, 44);
    lv_obj_add_event_cb(btn, on_click, LV_EVENT_CLICKED, NULL);
    s.count_label = lv_label_create(btn);
    lv_label_set_text(s.count_label, "Tap me");
    lv_obj_center(s.count_label);

    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_width(slider, LV_PCT(70));
    lv_obj_add_event_cb(slider, on_slider, LV_EVENT_VALUE_CHANGED, NULL);

    s.bar = lv_bar_create(scr);
    lv_obj_set_size(s.bar, LV_PCT(70), 14);

    s.value_label = lv_label_create(scr);
    lv_label_set_text(s.value_label, "0 %");
    lv_obj_set_style_text_color(s.value_label, lv_color_hex(0xB8C4D0), 0);

    lv_obj_t *sw = lv_switch_create(scr);
    lv_obj_add_state(sw, LV_STATE_CHECKED);

    s.status_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s.status_label, lv_color_hex(0x8FA3B5), 0);
    lv_obj_set_style_text_align(s.status_label, LV_TEXT_ALIGN_CENTER, 0);
    s.timer = lv_timer_create(on_timer, 500, NULL);
    on_timer(s.timer);
    sys_lvgl_unlock();
    return 0;
}

int sim_demo_fill(int count)
{
    sys_lvgl_lock();
    sys_lvgl_status_t st;
    err_h err = sys_lvgl_get_status(&st);
    err_free(err);
    lv_display_t *disp = err == NULL && st.state != SYS_LVGL_STATE_EMPTY ? lv_display_get_default() : NULL;
    if (disp == NULL) {
        sys_lvgl_unlock();
        return -1;
    }
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    int made = 0;
    for (; made < count; made++) {
        err = sys_lvgl_pool_check(256);  /* a label: about 240 B */
        if (err != NULL) {
            printf("  %d labels: refused\n", made);
            err_report(err);
            break;
        }
        lv_obj_t *l = lv_label_create(scr);
        lv_label_set_text_fmt(l, "label %d", made);
        if (made % 50 == 0) {
            err_free(sys_lvgl_get_status(&st));
            printf("  %d labels: pool %u of %u B used\n", made, (unsigned)st.heap_used, (unsigned)st.heap_budget);
            fflush(stdout);
        }
    }
    sys_lvgl_unlock();
    return made;
}
