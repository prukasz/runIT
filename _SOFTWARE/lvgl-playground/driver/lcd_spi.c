#include "lcd_spi.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_display_contract.h"
#include "sys_io.h"
#include "utils.h"

/* MIPI DCS commands */
#define LCD_SWRESET 0x01
#define LCD_SLPIN   0x10
#define LCD_SLPOUT  0x11
#define LCD_INVOFF  0x20
#define LCD_INVON   0x21
#define LCD_DISPON  0x29
#define LCD_CASET   0x2A
#define LCD_RASET   0x2B
#define LCD_RAMWR   0x2C
#define LCD_MADCTL  0x36
#define LCD_COLMOD  0x3A

/* MADCTL */
#define MADCTL_MY  0x80u
#define MADCTL_MX  0x40u
#define MADCTL_MV  0x20u
#define MADCTL_BGR 0x08u

#define COLMOD_16BIT    0x55u
/* The controller's timing (ILI9341 datasheet: RESX, SWRESET, SLPIN, SLPOUT) */
#define RESET_LOW_MS    1u    /* RESX low at least 10 us */
#define CMD_WAIT_MS     5u    /* after a reset, SLPOUT or SLPIN, before the next command */
#define SLEEP_SWAP_MS   120u  /* from a reset, SLPOUT or SLPIN to the next SLPOUT or SLPIN: the supplies settle */
#define CLEAR_BYTES     4800u /* 10 rows of 240 pixels: the zero buffer the frame memory is cleared with */
#define BYTES_PER_PIXEL 2u

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[15];
} lcd_spi_init_cmd_t;

typedef struct {
    uint16_t ram_w;  /* the frame memory, orientation 0 */
    uint16_t ram_h;
    const lcd_spi_init_cmd_t *init;
    uint8_t init_len;
} lcd_spi_panel_t;

/* Power, VCOM, frame rate, display function and gamma of the ILI9341 (its application notes) */
static const lcd_spi_init_cmd_t k_ili9341_init[] = {
    {0xCF, 3, {0x00, 0xC1, 0x30}},
    {0xED, 4, {0x64, 0x03, 0x12, 0x81}},
    {0xE8, 3, {0x85, 0x00, 0x78}},
    {0xCB, 5, {0x39, 0x2C, 0x00, 0x34, 0x02}},
    {0xF7, 1, {0x20}},
    {0xEA, 2, {0x00, 0x00}},
    {0xC0, 1, {0x23}},
    {0xC1, 1, {0x10}},
    {0xC5, 2, {0x3E, 0x28}},
    {0xC7, 1, {0x86}},
    {0xB1, 2, {0x00, 0x18}},
    {0xB6, 3, {0x08, 0x82, 0x27}},
    {0xF2, 1, {0x00}},
    {0x26, 1, {0x01}},
    {0xE0, 15, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}},
    {0xE1, 15, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}},
};

/* By sys_display_panel_e; a controller without an init sequence here is refused (ERR_DEV_RANGE) */
static const lcd_spi_panel_t k_panels[] = {
    [SYS_DISPLAY_PANEL_ILI9341] = {.ram_w = 240, .ram_h = 320, .init = k_ili9341_init,
                                   .init_len = ARRAY_LEN(k_ili9341_init)},
};

/* RDDPM (MIPI DCS 0x0A), the power mode: a panel that runs as the driver set it is awake with its picture on */
#define LCD_RDDPM          0x0A
#define RDDPM_SLEEP_OUT    0x10u
#define RDDPM_DISPLAY_ON   0x04u

/* MADCTL of each orientation (sys_display_orientation_e): rows and columns exchanged and mirrored so that the picture
   turns by 90 deg each step, the way the touch point is turned back (SYS_TOUCH.MD) */
static const uint8_t k_madctl[] = {0, MADCTL_MV | MADCTL_MY, MADCTL_MX | MADCTL_MY, MADCTL_MV | MADCTL_MX};

typedef struct {
    sys_device_base_t base;  /* first */
    display_cfg_t cfg;
    const lcd_spi_panel_t *panel;
    esp_lcd_panel_io_handle_t io;
    uint8_t *zero;           /* CLEAR_BYTES of zeros, DMA-capable */
    uint8_t orientation;
    uint16_t brightness;     /* the backlight's duty now (BACKLIGHT read) */
    bool awake;              /* the panel should be awake with its picture on: from the start to SUSPEND, RESUME on */
    TickType_t sleep_change; /* the last reset, SLPOUT or SLPIN: the next SLPOUT / SLPIN waits SLEEP_SWAP_MS from it */
    bool reset_used;
    bool backlight_used;
    /* the FLUSH in flight: set in FLUSH, ended in on_done (the transfer's interrupt) */
    volatile bool busy;
    display_flush_done_f done;
    void *done_ctx;
} lcd_spi_t;

static err_h io_err(const lcd_spi_t *d, esp_err_t e)
{
    ERR_CHECK(e == ESP_OK, ERR_DISPLAY_TRANSFER, .id = d->base.id, .code = e);
    return NULL;
}

static err_h cmd(const lcd_spi_t *d, uint8_t c, const void *data, size_t len)
{
    return io_err(d, esp_lcd_panel_io_tx_param(d->io, c, data, len));
}

static err_h cmd8(const lcd_spi_t *d, uint8_t c, uint8_t v)
{
    return cmd(d, c, &v, 1);
}

static uint16_t width_now(const lcd_spi_t *d)
{
    return (d->orientation & 1u) ? d->cfg.height : d->cfg.width;
}

static uint16_t height_now(const lcd_spi_t *d)
{
    return (d->orientation & 1u) ? d->cfg.width : d->cfg.height;
}

/* MADCTL's scan direction for an orientation: the turn, then the glass's mirror on the RAM's own axes (MX its
   columns, MY its rows: the controller mirrors after it exchanges them, MV) */
static uint8_t scan(const lcd_spi_t *d, uint8_t orientation)
{
    uint8_t m = k_madctl[orientation];
    if (d->cfg.flags & SYS_DISPLAY_FLAG_MIRROR_X) m ^= MADCTL_MX;
    if (d->cfg.flags & SYS_DISPLAY_FLAG_MIRROR_Y) m ^= MADCTL_MY;
    return m;
}

/* One axis of the window: where the glass starts on it as the controller counts (from the far end when mirrored) */
static uint16_t axis_offset(uint16_t ram, uint16_t glass, uint16_t offset, bool mirrored)
{
    return mirrored ? (uint16_t)(ram - glass - offset) : offset;
}

/* The glass's start in the frame memory as the controller addresses it in the current orientation: the window's
   columns run along the RAM's rows when MV exchanges them */
static void ram_offset(const lcd_spi_t *d, uint16_t *ox, uint16_t *oy)
{
    const display_cfg_t *c = &d->cfg;
    const uint16_t rw = d->panel->ram_w, rh = d->panel->ram_h;
    const uint8_t m = scan(d, d->orientation);
    if (m & MADCTL_MV) {
        *ox = axis_offset(rh, c->height, c->offset_y, m & MADCTL_MY);
        *oy = axis_offset(rw, c->width, c->offset_x, m & MADCTL_MX);
    } else {
        *ox = axis_offset(rw, c->width, c->offset_x, m & MADCTL_MX);
        *oy = axis_offset(rh, c->height, c->offset_y, m & MADCTL_MY);
    }
}

/* At least `ms`: vTaskDelay(n) ends at the n-th tick, which may come up to one tick less than n after the call */
static void wait_ms(uint32_t ms)
{
    vTaskDelay(MSEC(ms) + 1u);
}

/* Waits what is left of at least `ms` since `since` */
static void wait_since(TickType_t since, uint32_t ms)
{
    const TickType_t gone = xTaskGetTickCount() - since;
    if (gone <= MSEC(ms)) vTaskDelay(MSEC(ms) - gone + 1u);
}

/* SLPOUT or SLPIN: SLEEP_SWAP_MS after the last reset or sleep change, then CMD_WAIT_MS before the next command */
static err_h sleep_cmd(lcd_spi_t *d, uint8_t c)
{
    wait_since(d->sleep_change, SLEEP_SWAP_MS);
    ERR_TRY(cmd(d, c, NULL, 0));
    d->sleep_change = xTaskGetTickCount();
    wait_ms(CMD_WAIT_MS);
    return NULL;
}

static err_h set_window(const lcd_spi_t *d, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    const uint8_t ca[] = {x1 >> 8, x1 & 0xFF, x2 >> 8, x2 & 0xFF};
    const uint8_t ra[] = {y1 >> 8, y1 & 0xFF, y2 >> 8, y2 & 0xFF};
    ERR_TRY(cmd(d, LCD_CASET, ca, sizeof(ca)));
    return cmd(d, LCD_RASET, ra, sizeof(ra));
}

static err_h set_madctl(const lcd_spi_t *d, uint8_t orientation)
{
    const uint8_t bgr = (d->cfg.flags & SYS_DISPLAY_FLAG_BGR) ? MADCTL_BGR : 0u;
    return cmd8(d, LCD_MADCTL, (uint8_t)(scan(d, orientation) | bgr));
}

static err_h set_backlight(lcd_spi_t *d, uint16_t duty)
{
    if (d->backlight_used) {
        const uint32_t level =
            (d->cfg.flags & SYS_DISPLAY_FLAG_BL_ACTIVE_LOW) ? SYS_DISPLAY_BRIGHTNESS_MAX - duty : duty;
        ERR_TRY(sys_io_set_pwm_duty(d->cfg.backlight, level));
    }
    d->brightness = duty;
    return NULL;
}

/* The transfer's end (on the board: the SPI interrupt). The clear's transfers have no FLUSH: busy is false. */
static bool on_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    (void)io;
    (void)ev;
    lcd_spi_t *d = ctx;
    if (d->busy) {
        const display_flush_done_f done = d->done;
        void *const done_ctx = d->done_ctx;
        d->busy = false;
        done(done_ctx);
    }
    return false;
}

static err_h io_up(lcd_spi_t *d)
{
    const esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num         = sys_io_pin_valid(d->cfg.cs) ? d->cfg.cs.pin : -1,
        .dc_gpio_num         = d->cfg.dc.pin,
        .spi_mode            = d->cfg.spi_mode,
        .pclk_hz             = d->cfg.freq_hz,
        .trans_queue_depth   = 4,
        .on_color_trans_done = on_done,
        .user_ctx            = d,
        .lcd_cmd_bits        = 8,
        .lcd_param_bits      = 8,
    };
    return io_err(d, esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)d->cfg.bus, &io, &d->io));
}

/* Deletes the panel IO: waits for the transfer in flight (its done comes) or drops a hung one (no done after this). */
static void io_down(lcd_spi_t *d)
{
    if (d->io != NULL) {
        (void)esp_lcd_panel_io_del(d->io);
        d->io = NULL;
    }
    d->busy = false;
}

/* The power-on picture: every pixel of the frame memory black, in bands of CLEAR_BYTES, in the RAM's own order. */
static err_h clear_ram(const lcd_spi_t *d)
{
    const uint16_t rw = d->panel->ram_w, rh = d->panel->ram_h;
    ERR_TRY(cmd8(d, LCD_MADCTL, 0));
    const uint16_t rows = (uint16_t)(CLEAR_BYTES / (rw * BYTES_PER_PIXEL));
    for (uint16_t y = 0; y < rh; y += rows) {
        const uint16_t y2 = (uint16_t)(y + rows - 1u < rh ? y + rows - 1u : rh - 1u);
        ERR_TRY(set_window(d, 0, y, rw - 1u, y2));
        const size_t len = (size_t)rw * (y2 - y + 1u) * BYTES_PER_PIXEL;
        ERR_TRY(io_err(d, esp_lcd_panel_io_tx_color(d->io, LCD_RAMWR, d->zero, len)));
    }
    return NULL;
}

/* From reset to a lit, black screen in the config's orientation (the class comment's flow). The init sequence and the
   clear go to the sleeping controller while the reset's SLEEP_SWAP_MS runs out (the panel may have been awake before
   it), so SLPOUT waits only what is left of it. */
static err_h panel_start(lcd_spi_t *d)
{
    ERR_TRY(io_up(d));
    if (d->reset_used) {
        ERR_TRY(sys_io_set_level(d->cfg.reset, false));
        wait_ms(RESET_LOW_MS);
        ERR_TRY(sys_io_set_level(d->cfg.reset, true));
    } else {
        ERR_TRY(cmd(d, LCD_SWRESET, NULL, 0));
    }
    d->sleep_change = xTaskGetTickCount();
    wait_ms(CMD_WAIT_MS);
    for (uint8_t i = 0; i < d->panel->init_len; i++) {
        const lcd_spi_init_cmd_t *c = &d->panel->init[i];
        ERR_TRY(cmd(d, c->cmd, c->data, c->len));
    }
    ERR_TRY(cmd8(d, LCD_COLMOD, COLMOD_16BIT));
    ERR_TRY(cmd(d, (d->cfg.flags & SYS_DISPLAY_FLAG_INVERT) ? LCD_INVON : LCD_INVOFF, NULL, 0));
    ERR_TRY(clear_ram(d));
    ERR_TRY(sleep_cmd(d, LCD_SLPOUT));
    d->orientation = d->cfg.orientation;
    ERR_TRY(set_madctl(d, d->orientation));
    ERR_TRY(cmd(d, LCD_DISPON, NULL, 0));
    d->awake = true;
    return set_backlight(d, d->cfg.brightness_default);
}

/* ---- DISPLAY contract ---- */

OP_READ(DISPLAY, GEOMETRY)
{
    (void)args;
    const lcd_spi_t *d = ctx;
    const display_geometry_t g = {.width = width_now(d), .height = height_now(d),
                                  .format = SYS_DISPLAY_FORMAT_RGB565_SWAPPED, .flush_max = LCD_SPI_FLUSH_MAX};
    return cmd_reply(out, &g, sizeof(g));
}

OP_WRITE(DISPLAY, BACKLIGHT)
{
    (void)out;
    lcd_spi_t *d = ctx;
    SYS_DEV_CHECK_IN_RANGE(d->base.id, args->brightness_duty, 0, SYS_DISPLAY_BRIGHTNESS_MAX);
    return set_backlight(d, args->brightness_duty);
}

OP_READ(DISPLAY, BACKLIGHT)
{
    (void)args;
    const display_backlight_args_t b = {.brightness_duty = ((const lcd_spi_t *)ctx)->brightness};
    return cmd_reply(out, &b, sizeof(b));
}

/* RDDPM over the bus (a READABLE module only): nothing driving MISO reads 0xFF (the bus's pull-up) or 0x00; a panel
   that answers but sleeps or shows nothing while it should run lost its power (RESTARTED). */
OP_READ(DISPLAY, HEALTH)
{
    (void)args;
    lcd_spi_t *d = ctx;
    display_health_t h = {.health = SYS_DISPLAY_HEALTH_UNKNOWN, .power_mode = 0};
    if ((d->cfg.flags & SYS_DISPLAY_FLAG_READABLE) != 0u && d->io != NULL) {
        ERR_TRY(io_err(d, esp_lcd_panel_io_rx_param(d->io, LCD_RDDPM, &h.power_mode, 1)));
        const uint8_t running = RDDPM_SLEEP_OUT | RDDPM_DISPLAY_ON;
        if (h.power_mode == 0xFFu || h.power_mode == 0x00u) {
            h.health = SYS_DISPLAY_HEALTH_NO_ANSWER;
        } else if (d->awake && (h.power_mode & running) != running) {
            h.health = SYS_DISPLAY_HEALTH_RESTARTED;
        } else {
            h.health = SYS_DISPLAY_HEALTH_OK;
        }
    }
    return cmd_reply(out, &h, sizeof(h));
}

/* CASET, RASET, RAMWR with the pixels queued; returns before they have gone. Nothing is known about a transfer once
   it was queued (SPI has no acknowledge). */
OP_WRITE(DISPLAY, FLUSH)
{
    (void)out;
    lcd_spi_t *d = ctx;
    const display_window_t *w = &args->window;
    ERR_CHECK(args->pixels != NULL && args->done != NULL && esp_ptr_dma_capable(args->pixels), ERR_DEV_BAD_ARG);
    const bool inside = w->x1 <= w->x2 && w->y1 <= w->y2 && w->x2 < width_now(d) && w->y2 < height_now(d);
    const uint32_t bytes =
        inside ? (uint32_t)(w->x2 - w->x1 + 1) * (uint32_t)(w->y2 - w->y1 + 1) * BYTES_PER_PIXEL : 0u;
    ERR_CHECK(inside && bytes == args->len && args->len <= LCD_SPI_FLUSH_MAX, ERR_DISPLAY_BAD_WINDOW,
              .id = d->base.id, .x1 = w->x1, .y1 = w->y1, .x2 = w->x2, .y2 = w->y2);
    ERR_CHECK(!d->busy, ERR_DISPLAY_FLUSH_BUSY, .id = d->base.id);
    ERR_CHECK(d->io != NULL, ERR_DISPLAY_TRANSFER, .id = d->base.id, .code = ESP_ERR_INVALID_STATE);
    uint16_t ox, oy;
    ram_offset(d, &ox, &oy);
    ERR_TRY(set_window(d, w->x1 + ox, w->y1 + oy, w->x2 + ox, w->y2 + oy));
    d->done = args->done;
    d->done_ctx = args->done_ctx;
    d->busy = true;
    const esp_err_t e = esp_lcd_panel_io_tx_color(d->io, LCD_RAMWR, args->pixels, args->len);
    if (e != ESP_OK) {
        d->busy = false;
    }
    return io_err(d, e);
}

OP_WRITE(DISPLAY, ORIENTATION)
{
    (void)out;
    lcd_spi_t *d = ctx;
    SYS_DEV_CHECK_IN_RANGE(d->base.id, args->orientation, SYS_DISPLAY_ORIENTATION_0, SYS_DISPLAY_ORIENTATION_270);
    ERR_CHECK(!d->busy, ERR_DISPLAY_FLUSH_BUSY, .id = d->base.id);
    ERR_TRY(set_madctl(d, args->orientation));
    d->orientation = args->orientation;
    return NULL;
}

OP_READ(DISPLAY, ORIENTATION)
{
    (void)args;
    const display_orientation_args_t o = {.orientation = ((const lcd_spi_t *)ctx)->orientation};
    return cmd_reply(out, &o, sizeof(o));
}

/* ---- Lifecycle ---- */

/* The power-on state again: the panel IO new (a hung transfer dropped), the whole flow. */
OP_WRITE(SYS_DEVICE, RESET)
{
    (void)args;
    (void)out;
    lcd_spi_t *d = ctx;
    io_down(d);
    return panel_start(d);
}

/* The backlight off, then SLPIN: the panel IO sends it after the transfer in flight, whose done still comes. */
static err_h suspend_hook(void *ctx)
{
    lcd_spi_t *d = ctx;
    ERR_TRY(set_backlight(d, 0));
    d->awake = false;
    return d->io != NULL ? sleep_cmd(d, LCD_SLPIN) : NULL;
}

static err_h resume_hook(void *ctx)
{
    lcd_spi_t *d = ctx;
    ERR_CHECK(d->io != NULL, ERR_DISPLAY_TRANSFER, .id = d->base.id, .code = ESP_ERR_INVALID_STATE);
    ERR_TRY(sleep_cmd(d, LCD_SLPOUT));
    d->awake = true;
    return set_backlight(d, d->cfg.brightness_default);
}

static err_h uninstall(void *ctx)
{
    lcd_spi_t *d = ctx;
    if (d->backlight_used) {
        err_free(set_backlight(d, 0));
        err_free(sys_io_release(d->cfg.backlight));
    }
    io_down(d);
    if (d->reset_used) {
        err_free(sys_io_release(d->cfg.reset));
    }
    heap_caps_free(d->zero);
    d->zero = NULL;
    return NULL;
}

static err_h create(void *ctx, const void *cfg_in)
{
    lcd_spi_t *d = ctx;
    memcpy(&d->cfg, cfg_in, sizeof(d->cfg));
    const display_cfg_t *c = &d->cfg;
    const uint16_t id = d->base.id;
    const bool known = c->panel < ARRAY_LEN(k_panels) && k_panels[c->panel].init != NULL;
    ERR_CHECK(known, ERR_DEV_RANGE, .id = id, .value = c->panel, .min = 0, .max = ARRAY_LEN(k_panels) - 1);
    d->panel = &k_panels[c->panel];
    SYS_DEV_CHECK_IN_RANGE(id, c->spi_mode, 0, 3);
    SYS_DEV_CHECK_IN_RANGE(id, c->freq_hz, 1, 80000000);
    SYS_DEV_CHECK_IN_RANGE(id, c->width, 1, d->panel->ram_w - c->offset_x);
    SYS_DEV_CHECK_IN_RANGE(id, c->height, 1, d->panel->ram_h - c->offset_y);
    SYS_DEV_CHECK_IN_RANGE(id, c->orientation, SYS_DISPLAY_ORIENTATION_0, SYS_DISPLAY_ORIENTATION_270);
    SYS_DEV_CHECK_IN_RANGE(id, c->brightness_default, 0, SYS_DISPLAY_BRIGHTNESS_MAX);
    SYS_DEV_CHECK_IN_RANGE(id, c->flags, 0, SYS_DISPLAY_FLAGS_ALL);
    ERR_CHECK(sys_io_pin_valid(c->dc), ERR_DEV_BAD_PIN, .id = id, .pin = c->dc.pin);

    d->zero = heap_caps_calloc(1, CLEAR_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ERR_CHECK(d->zero != NULL, ERR_DEV_BAD_ARG);
    if (sys_io_pin_valid(c->reset)) {
        ERR_TRY(sys_device_use(&d->base, c->reset.device));
        ERR_TRY(sys_io_prepare(c->reset, SYS_IO_MODE_OUTPUT_PUSH_PULL));
        d->reset_used = true;
    }
    if (sys_io_pin_valid(c->backlight)) {
        ERR_TRY(sys_device_use(&d->base, c->backlight.device));
        ERR_TRY(sys_io_prepare(c->backlight, SYS_IO_MODE_PWM));
        d->backlight_used = true;
    }
    return panel_start(d);
}

/* ---- Type ---- */

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(SYS_DEVICE, RESET),
    OP_ENTRY_R(DISPLAY, GEOMETRY),
    OP_ENTRY(DISPLAY, BACKLIGHT),
    OP_ENTRY_W(DISPLAY, FLUSH),
    OP_ENTRY(DISPLAY, ORIENTATION),
    OP_ENTRY_R(DISPLAY, HEALTH),
};

static const sys_device_type_t k_type = {
    .type      = LCD_SPI_DEVICE_TYPE,
    .cfg_size  = sizeof(display_cfg_t),
    .ctx_size  = sizeof(lcd_spi_t),
    .create    = create,
    .uninstall = uninstall,
    .suspend   = suspend_hook,
    .resume    = resume_hook,
    .ops       = k_ops,
    .op_count  = ARRAY_LEN(k_ops),
};

SYS_DEVICE_TYPE_REGISTER(k_type)
