#include "sim_scenario.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mock_panel.h"
#include "mock_touch.h"
#include "router.h"
#include "sim_demo.h"
#include "mock_ili9341.h"
#include "mock_spi.h"
#include "sys_lvgl_router.h"

static void sleep_ms(unsigned ms)
{
    struct timespec ts = {.tv_sec = ms / 1000u, .tv_nsec = (long)(ms % 1000u) * 1000000L};
    nanosleep(&ts, NULL);
}

static void print_hex(const char *prefix, const uint8_t *p, uint32_t n)
{
    printf("%s", prefix);
    for (uint32_t i = 0; i < n; i++) printf(" %02X", p[i]);
    printf("\n");
}

static const char *k_states[] = {"EMPTY", "RUNNING", "STOPPED", "PAUSED"};

static void print_status(const uint8_t *p, uint32_t n)
{
    sys_lvgl_status_t st;
    if (n != sizeof(st)) return;
    memcpy(&st, p, sizeof(st));
    printf("  state %s, %ux%u, rotation %u deg, %u buffer(s) of %u B\n", st.state < 4 ? k_states[st.state] : "?",
           st.width, st.height, st.rotation * 90u, st.buffers, (unsigned)st.buffer_bytes);
    printf("  heap %u of %u B (peak %u), flushes %u, flush errors %u\n", (unsigned)st.heap_used,
           (unsigned)st.heap_budget, (unsigned)st.heap_peak, (unsigned)st.flushes, (unsigned)st.flush_errors);
}

int sim_frame(const uint8_t *req, uint32_t len)
{
    static uint8_t buf[CMD_DATA_MAX];
    cmd_out_t out = {.data = buf, .cap = sizeof(buf), .len = 0};
    print_hex(">", req, len);
    err_h err = router_route((cmd_in_t){.data = req, .len = len}, &out);
    if (err != NULL) {
        printf("! error (logged above as the chain)\n");
        err_report(err);
        return 1;
    }
    print_hex("<", buf, out.len);
    if (len >= 3 && req[0] == ROUTER_KEY_LVGL && req[1] == CMD_DIR_READ && req[2] == SYS_LVGL_OP_STATUS) {
        print_status(buf, out.len);
    }
    return 0;
}

static int run_hex(const char *text)
{
    uint8_t req[CMD_DATA_MAX];
    uint32_t n = 0;
    const char *p = text;
    while (*p != '\0') {
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') break;
        char *end;
        const unsigned long v = strtoul(p, &end, 16);
        if (end == p || v > 0xFF || n == sizeof(req)) {
            printf("! bad hex at \"%s\"\n", p);
            return 1;
        }
        req[n++] = (uint8_t)v;
        p = end;
    }
    return sim_frame(req, n);
}

static int run_line(char *line, const char *out_dir)
{
    char *cmd = line;
    while (isspace((unsigned char)*cmd)) cmd++;
    cmd[strcspn(cmd, "\r\n")] = '\0';
    if (*cmd == '\0' || *cmd == '#') return 0;
    if (*cmd == '>') return run_hex(cmd + 1);

    char word[16] = {0}, arg[200] = {0};
    sscanf(cmd, "%15s %199[^\n]", word, arg);
    if (!strcmp(word, "wait")) {
        sleep_ms((unsigned)atoi(arg));
        return 0;
    }
    if (!strcmp(word, "tap")) {
        int x = 0, y = 0;
        if (sscanf(arg, "%d %d", &x, &y) != 2) goto bad;
        printf("tap %d %d\n", x, y);
        mock_touch_set(x, y, true);
        sleep_ms(100);
        mock_touch_set(x, y, false);
        sleep_ms(100);
        return 0;
    }
    if (!strcmp(word, "fault")) {
        static const char *k_names[] = {"none", "refuse", "noise", "hang"};
        for (int i = 0; i < 4; i++) {
            if (!strcmp(arg, k_names[i])) {
                printf("fault %s\n", arg);
                mock_spi_set_fault((mock_spi_fault_e)i);
                return 0;
            }
        }
        goto bad;
    }
    if (!strcmp(word, "spi")) {
        if (strcmp(arg, "on") != 0 && strcmp(arg, "off") != 0) goto bad;
        mock_spi_set_trace(!strcmp(arg, "on"));
        return 0;
    }
    if (!strcmp(word, "cable")) {
        if (strcmp(arg, "on") != 0 && strcmp(arg, "off") != 0) goto bad;
        printf("cable %s\n", arg);
        mock_ili9341_cable(!strcmp(arg, "on"));
        return 0;
    }
    if (!strcmp(word, "glass")) {
        static const char *k_mirrors[] = {"mirror none", "mirror x", "mirror y", "mirror xy"};
        for (int i = 0; i < 4; i++) {
            if (!strcmp(arg, k_mirrors[i])) {
                printf("glass %s\n", arg);
                mock_ili9341_set_glass_mirror((i & 1) != 0, (i & 2) != 0);
                return 0;
            }
        }
        goto bad;
    }
    if (!strcmp(word, "panel")) {
        return mock_ili9341_print() != 0u;
    }
    if (!strcmp(word, "demo")) {
        printf("demo screen\n");
        return sim_demo_build() == 0 ? 0 : (printf("! the screen is not installed\n"), 1);
    }
    if (!strcmp(word, "fill")) {
        const int made = sim_demo_fill(atoi(arg));
        printf("fill: %d label(s) made\n", made);
        return made < 0;
    }
    if (!strcmp(word, "shot")) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.bmp", out_dir, arg);
        const int rc = mock_panel_save_bmp(path);
        printf("shot %s %s\n", path, rc ? "FAILED" : "saved");
        return rc ? 1 : 0;
    }
    if (!strcmp(word, "echo")) {
        printf("-- %s\n", arg);
        return 0;
    }
bad:
    printf("! bad line: %s\n", cmd);
    return 1;
}

int sim_scenario_run(const char *path, const char *out_dir)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        printf("! cannot read %s\n", path);
        return -1;
    }
    char line[1024];
    int failed = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        failed += run_line(line, out_dir);
        fflush(stdout);
    }
    fclose(f);
    printf("scenario done: %d command(s) failed\n", failed);
    return failed;
}
