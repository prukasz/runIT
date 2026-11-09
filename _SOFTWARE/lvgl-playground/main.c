/* The screen simulator: the firmware's own sys_lvgl, sys_device, sys_display and sys_touch on the PC, with the display
 * driver (driver/) on a simulated SPI bus and ILI9341 (mock/), a simulated touch and GPIO (sim/). A scenario (scenarios/) sends command frames through the
 * real router, as the app would over the session; the window shows the glass, the mouse is the finger.
 *   playground [--window SCALE] [--out DIR] SCENARIO */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_mocks.h"
#include "mock_ili9341.h"
#include "mock_panel.h"
#include "router.h"
#include "sim_demo.h"
#include "sim_scenario.h"
#include "sys_device.h"
#include "sys_device_router.h"
#include "sys_lvgl.h"
#include "sys_lvgl_router.h"
#ifdef HAVE_WINDOW
#include "mock_window.h"
#endif

/* runit's state listener does the same for the screen (runit_error_policy.c). */
static void device_state_changed(uint16_t id, sys_device_state_e change)
{
    static const char *k_names[] = {"SUSPENDED", "RESUMED", "RESET"};
    printf("event DEVICE %s, device %u\n", (unsigned)change < 3 ? k_names[change] : "?", (unsigned)id);
    sys_lvgl_device_state(id, change);
}

static int boot(void)
{
    host_time_real(true);
    host_log_echo(true);
    if (mock_ili9341_power_on() != 0) return -1;  /* the module has power before anything runs */
    sys_device_set_state_listener(device_state_changed);
    err_h err = router_register(ROUTER_KEY_DEVICE, sys_device_router);
    if (err == NULL) err = router_register(ROUTER_KEY_LVGL, sys_lvgl_router);
    if (err == NULL) err = sys_lvgl_init();
    if (err != NULL) {
        err_report(err);
        return -1;
    }
    sim_demo_init();
    return 0;
}

static const char *s_scenario;
static const char *s_out = "out";
static volatile int s_failed;
static volatile int s_scenario_done;

static void *scenario_thread(void *arg)
{
    (void)arg;
    s_failed = sim_scenario_run(s_scenario, s_out);
    s_scenario_done = 1;
    return NULL;
}

#ifdef HAVE_WINDOW
/* R: the next rotation, P: STOP / START, X: RESET of the screen, N: INSTALL (display 1, touch 2) and the demo,
   D: the demo screen. As frames, like the app. */
static void on_key(int key)
{
    static uint8_t rotation;
    static int stopped;
    if (key == 'r') {
        rotation = (uint8_t)((rotation + 1u) & 3u);
        const uint8_t f[] = {ROUTER_KEY_LVGL, CMD_DIR_WRITE, SYS_LVGL_OP_ROTATION, rotation};
        sim_frame(f, sizeof f);
    } else if (key == 'p') {
        stopped = !stopped;
        const uint8_t f[] = {ROUTER_KEY_LVGL, CMD_DIR_WRITE, stopped ? SYS_LVGL_OP_STOP : SYS_LVGL_OP_START};
        sim_frame(f, sizeof f);
    } else if (key == 'x') {
        const uint8_t f[] = {ROUTER_KEY_LVGL, CMD_DIR_WRITE, SYS_LVGL_OP_RESET};
        sim_frame(f, sizeof f);
    } else if (key == 'i') {
        const uint8_t f[] = {ROUTER_KEY_LVGL, CMD_DIR_READ, SYS_LVGL_OP_STATUS};
        sim_frame(f, sizeof f);
    } else if (key == 'n') {
        const uint8_t f[] = {ROUTER_KEY_LVGL, CMD_DIR_WRITE, SYS_LVGL_OP_INSTALL, 1, 0, 2, 0, 0, 0, 0, 0};
        if (sim_frame(f, sizeof f) == 0) {
            sim_demo_build();
            stopped = 0;
        }
    } else if (key == 'd') {
        sim_demo_build();
    }
    fflush(stdout);
}

static int run_window(int scale)
{
    pthread_t th;
    pthread_create(&th, NULL, scenario_thread, NULL);
    int opened = 0, open = 1;
    uint32_t seen = 0, last_ms = 0, last_transfers = 0, last_bytes = 0;
    while (open) {
        int w, h;
        mock_panel_size(&w, &h);
        if (!opened && w > 0) {
            if (mock_window_open("runIT screen", w, h, scale)) {
                fprintf(stderr, "cannot open the window\n");
                return 1;
            }
            mock_window_on_key(on_key);
            opened = 1;
            last_ms = mock_window_ticks();
        }
        if (!opened) {
            if (s_scenario_done) break;  /* the scenario made no display */
            struct timespec ts = {0, 10 * 1000000L};
            nanosleep(&ts, NULL);
            continue;
        }
        open = mock_window_poll();
        const mock_panel_stats_t *st = mock_panel_stats();
        if (st->changes != seen) {
            seen = st->changes;
            mock_window_present();
        }
        const uint32_t now = mock_window_ticks();
        if (now - last_ms >= 1000) {
            char title[128];
            snprintf(title, sizeof title, "runIT screen | %u transfers/s, %u kB/s | R rotate, P stop/start, X reset, N install, I status",
                     (unsigned)(st->transfers - last_transfers), (unsigned)((st->bus_bytes - last_bytes) / 1024));
            mock_window_set_title(title);
            last_ms = now;
            last_transfers = st->transfers;
            last_bytes = st->bus_bytes;
        }
        mock_window_delay(5);
    }
    if (opened) mock_window_close();
    return s_failed > 0;
}
#endif

int main(int argc, char **argv)
{
    int scale = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--window") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) s_out = argv[++i];
        else s_scenario = argv[i];
    }
    if (s_scenario == NULL) {
        fprintf(stderr, "usage: playground [--window SCALE] [--out DIR] SCENARIO\n");
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    if (boot()) return 1;
#ifdef HAVE_WINDOW
    if (scale > 0) return run_window(scale);
#else
    if (scale > 0) {
        fprintf(stderr, "built without PLAYGROUND_WINDOW\n");
        return 1;
    }
#endif
    scenario_thread(NULL);
    return s_failed != 0;
}
