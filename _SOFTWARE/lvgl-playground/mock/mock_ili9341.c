#include "mock_ili9341.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mock_panel.h"

#define W MOCK_ILI9341_WIDTH
#define H MOCK_ILI9341_HEIGHT

/* MADCTL bits */
#define MY  0x80u
#define MX  0x40u
#define MV  0x20u
#define BGR 0x08u

/* Commands the model knows: what they do, or only how many parameters they take (power, gamma, timing). */
#define C_NOP     0x00
#define C_SWRESET 0x01
#define C_SLPIN   0x10
#define C_SLPOUT  0x11
#define C_INVOFF  0x20
#define C_INVON   0x21
#define C_DISPOFF 0x28
#define C_DISPON  0x29
#define C_CASET   0x2A
#define C_RASET   0x2B
#define C_RAMWR   0x2C
#define C_MADCTL  0x36
#define C_COLMOD  0x3A

static const uint8_t k_known[] = {
    C_NOP, C_SWRESET, C_SLPIN, C_SLPOUT, C_INVOFF, C_INVON, C_DISPOFF, C_DISPON, C_CASET, C_RASET, C_RAMWR,
    C_MADCTL, C_COLMOD, 0x0A /* RDDPM: read */,
    0x26 /* gamma set */, 0xB1 /* frame rate */, 0xB6 /* display function */, 0xC0, 0xC1 /* power */,
    0xC5, 0xC7 /* VCOM */, 0xE0, 0xE1 /* gamma */, 0xCB, 0xCF, 0xE8, 0xEA, 0xED, 0xF2, 0xF7 /* extended */,
};

static struct {
    pthread_mutex_t m;
    bool up;
    uint16_t gram[W * H];  /* RGB565 as the chip stores it */
    bool reset_held;       /* RESX low */
    bool sleep;
    bool on;
    bool inverted;
    uint8_t madctl;
    uint8_t colmod;
    uint16_t sc, ec, sp, ep;  /* the window: columns, pages */
    uint16_t col, page;       /* the write pointer */
    uint8_t cmd;
    uint8_t params[16];
    unsigned nparams;
    bool writing;             /* after RAMWR */
    uint8_t carry[3];         /* a pixel split across two data chunks */
    unsigned ncarry;
    uint64_t ready_ms;        /* no command before this (a reset, SLPOUT, SLPIN) */
    uint64_t sleep_swap_ms;   /* no SLPOUT / SLPIN before this (a reset while awake, SLPOUT, SLPIN) */
    bool mirror_x, mirror_y;  /* the glass mounted mirrored against the frame memory */
    bool cut;                 /* the cable is off: no power, nothing taken, reads 0xFF */
    unsigned warnings;
} s = {.m = PTHREAD_MUTEX_INITIALIZER};

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void warn(const char *what, unsigned cmd)
{
    s.warnings++;
    printf("ili9341: ! %s (command %02X)\n", what, cmd);
}

static bool visible(void)
{
    return s.up && s.on && !s.sleep && !s.reset_held;
}

/* What the glass shows for a stored pixel: the module's glass is BGR, MADCTL's BGR bit swaps the order back. */
static uint16_t shown(uint16_t c)
{
    if ((s.madctl & BGR) == 0u) c = (uint16_t)(((c & 0x1Fu) << 11) | (c & 0x07E0u) | (c >> 11));
    return s.inverted ? (uint16_t)~c : c;
}

/* A pixel of the frame memory onto the glass, through the way the glass is mounted */
static void put(int x, int y, uint16_t c)
{
    mock_panel_put(s.mirror_x ? W - 1 - x : x, s.mirror_y ? H - 1 - y : y, c);
}

static void present_all(void)
{
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            put(x, y, visible() ? shown(s.gram[y * W + x]) : 0u);
        }
    }
    mock_panel_redraw();
}

/* The registers after a reset (hardware, software, power-on); the frame memory stays. A reset while awake needs
   120 ms before SLPOUT. */
static void registers_reset(void)
{
    const uint64_t now = now_ms();
    if (!s.sleep) s.sleep_swap_ms = now + 120u;
    s.sleep = true;
    s.on = false;
    s.inverted = false;
    s.madctl = 0;
    s.colmod = 0x66;  /* 18 bit */
    s.sc = 0;
    s.ec = W - 1;
    s.sp = 0;
    s.ep = H - 1;
    s.writing = false;
    s.ncarry = 0;
    s.ready_ms = now + 5u;
}

void mock_ili9341_set_glass_mirror(bool x, bool y)
{
    pthread_mutex_lock(&s.m);
    s.mirror_x = x;
    s.mirror_y = y;
    present_all();
    pthread_mutex_unlock(&s.m);
}

/* Power on: noise in the frame memory, asleep (no 120 ms to wait), every register at its reset value */
static void chip_power_on(void)
{
    static uint32_t r = 0x2545F491u;
    for (int i = 0; i < W * H; i++) {
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        s.gram[i] = (uint16_t)r;
    }
    s.up = true;
    s.reset_held = false;
    s.sleep = true;
    registers_reset();
    present_all();
}

int mock_ili9341_power_on(void)
{
    if (mock_panel_init(W, H) != 0) return -1;
    pthread_mutex_lock(&s.m);
    chip_power_on();
    pthread_mutex_unlock(&s.m);
    return 0;
}

void mock_ili9341_cable(bool on)
{
    pthread_mutex_lock(&s.m);
    if (!on && !s.cut) {
        s.cut = true;
        s.up = false;
        present_all();
    } else if (on && s.cut) {
        s.cut = false;
        chip_power_on();
    }
    pthread_mutex_unlock(&s.m);
}

void mock_ili9341_read(void *out, size_t len)
{
    pthread_mutex_lock(&s.m);
    uint8_t v = 0;
    if (s.cut) {
        v = 0xFF;
    } else if (s.cmd == 0x0A) {  /* RDDPM */
        v = (uint8_t)((s.sleep ? 0u : 0x90u) | 0x08u | (s.on ? 0x04u : 0u));
    }
    memset(out, v, len);
    pthread_mutex_unlock(&s.m);
}

void mock_ili9341_resx(bool level)
{
    pthread_mutex_lock(&s.m);
    if (!level) {
        s.reset_held = true;
    } else if (s.reset_held) {
        s.reset_held = false;
        registers_reset();
    }
    present_all();
    pthread_mutex_unlock(&s.m);
}

/* A logical pixel (column, page in the current MADCTL) to the frame memory: rows and columns exchanged (MV), then
   mirrored (MX, MY). */
static void store(uint16_t col, uint16_t page, uint16_t c)
{
    int x = col, y = page;
    if (s.madctl & MV) {
        x = page;
        y = col;
    }
    if (s.madctl & MX) x = W - 1 - x;
    if (s.madctl & MY) y = H - 1 - y;
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    s.gram[y * W + x] = c;
    if (visible()) put(x, y, shown(c));
}

static void pixel(uint16_t c)
{
    store(s.col, s.page, c);
    if (++s.col > s.ec) {
        s.col = s.sc;
        if (++s.page > s.ep) s.page = s.sp;
    }
}

static unsigned need(uint8_t cmd)
{
    switch (cmd) {
    case C_CASET: case C_RASET: return 4;
    case C_MADCTL: case C_COLMOD: return 1;
    default: return 0;
    }
}

static void apply(void)
{
    const uint8_t *p = s.params;
    switch (s.cmd) {
    case C_CASET:
        s.sc = (uint16_t)(p[0] << 8 | p[1]);
        s.ec = (uint16_t)(p[2] << 8 | p[3]);
        break;
    case C_RASET:
        s.sp = (uint16_t)(p[0] << 8 | p[1]);
        s.ep = (uint16_t)(p[2] << 8 | p[3]);
        break;
    case C_MADCTL:
        s.madctl = p[0];
        present_all();
        break;
    case C_COLMOD:
        s.colmod = p[0];
        break;
    default:
        break;
    }
}

void mock_ili9341_command(uint8_t cmd)
{
    pthread_mutex_lock(&s.m);
    const uint64_t now = now_ms();
    s.cmd = cmd;
    s.nparams = 0;
    s.writing = false;
    s.ncarry = 0;
    if (s.cut) {
        pthread_mutex_unlock(&s.m);
        return;
    }
    if (!s.up || s.reset_held) {
        warn("command while the chip is off or held in reset", cmd);
        pthread_mutex_unlock(&s.m);
        return;
    }
    if (now < s.ready_ms) warn("command within 5 ms of a reset, SLPOUT or SLPIN", cmd);
    if (memchr(k_known, cmd, sizeof(k_known)) == NULL) warn("unknown command", cmd);
    switch (cmd) {
    case C_SWRESET:
        registers_reset();
        present_all();
        break;
    case C_SLPOUT:
    case C_SLPIN:
        if (now < s.sleep_swap_ms) warn("SLPOUT / SLPIN within 120 ms of a reset while awake, SLPOUT or SLPIN", cmd);
        s.sleep = cmd == C_SLPIN;
        s.sleep_swap_ms = now + 120u;
        s.ready_ms = now + 5u;
        present_all();
        break;
    case C_DISPON:
    case C_DISPOFF:
        s.on = cmd == C_DISPON;
        present_all();
        break;
    case C_INVON:
    case C_INVOFF:
        s.inverted = cmd == C_INVON;
        present_all();
        break;
    case C_RAMWR:
        s.writing = true;
        s.col = s.sc;
        s.page = s.sp;
        break;
    default:
        break;
    }
    pthread_mutex_unlock(&s.m);
}

void mock_ili9341_data(const void *data, size_t len)
{
    const uint8_t *b = data;
    pthread_mutex_lock(&s.m);
    if (!s.up || s.reset_held) {
        pthread_mutex_unlock(&s.m);
        return;
    }
    if (s.writing) {
        const unsigned bpp = (s.colmod & 0x07u) == 0x05u ? 2u : 3u;  /* DBI 16 bit, else 18 bit in 3 bytes */
        for (size_t i = 0; i < len; i++) {
            s.carry[s.ncarry++] = b[i];
            if (s.ncarry < bpp) continue;
            s.ncarry = 0;
            const uint16_t c = bpp == 2u ? (uint16_t)(s.carry[0] << 8 | s.carry[1])
                                         : (uint16_t)((s.carry[0] >> 3) << 11 | (s.carry[1] >> 2) << 5 | s.carry[2] >> 3);
            pixel(c);
        }
    } else if (need(s.cmd) == 0u) {
        if (s.cmd == C_NOP || s.cmd == C_SWRESET || s.cmd == C_SLPOUT || s.cmd == C_DISPON) {
            warn("data after a command that takes none", s.cmd);
        }
    } else {
        for (size_t i = 0; i < len && s.nparams < sizeof(s.params); i++) {
            s.params[s.nparams++] = b[i];
            if (s.nparams == need(s.cmd)) apply();
        }
    }
    pthread_mutex_unlock(&s.m);
}

unsigned mock_ili9341_print(void)
{
    pthread_mutex_lock(&s.m);
    printf("ili9341: %s, %s, %s, MADCTL %02X (MY %u MX %u MV %u BGR %u), COLMOD %02X, inversion %s, "
           "window %u..%u x %u..%u, warnings %u\n",
           s.reset_held ? "held in reset" : "running", s.sleep ? "asleep" : "awake", s.on ? "display on" : "display off",
           s.madctl, !!(s.madctl & MY), !!(s.madctl & MX), !!(s.madctl & MV), !!(s.madctl & BGR), s.colmod,
           s.inverted ? "on" : "off", s.sc, s.ec, s.sp, s.ep, s.warnings);
    const unsigned w = s.warnings;
    pthread_mutex_unlock(&s.m);
    return w;
}
