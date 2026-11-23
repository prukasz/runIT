/**
 * @file
 * @brief device_spi_lcd: sterownik wyświetlacza SPI (MIPI DCS) z kontraktem sys_display. Jeden
 *        sterownik dla wielu układów; układ (display_cfg_t.panel) wybiera tabelę z sekwencją
 *        inicjalizacji i bazowym MADCTL. Tabele z przykładu ESP-IDF spi_master/lcd
 *        (ESP-WROVER-KIT), kolejność jak w LVGL lv_lcd_generic_mipi: reset, SLPOUT, tabela.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"
#include "sys_device_driver.h"
#include "sys_display/sys_display_contract.h"
#include "sys_io.h"
#include "sys_io_driver.h"
#include "sys_spi/sys_spi.h"

SYS_DEVICE_TYPE_DECLARE(DEVICE_SPI_LCD, 0x0050)

/* Komendy MIPI DCS */
#define CMD_SWRESET 0x01u
#define CMD_SLPIN 0x10u
#define CMD_SLPOUT 0x11u
#define CMD_INVOFF 0x20u
#define CMD_INVON 0x21u
#define CMD_DISPOFF 0x28u
#define CMD_DISPON 0x29u
#define CMD_RDDPM 0x0Au
#define CMD_CASET 0x2Au
#define CMD_RASET 0x2Bu
#define CMD_RAMWR 0x2Cu
#define CMD_MADCTL 0x36u
#define CMD_COLMOD 0x3Au

#define COLMOD_RGB565 0x55u

/* Bity RDDPM: wyjście ze snu, wyświetlanie włączone */
#define RDDPM_SLPOUT 0x10u
#define RDDPM_DISPON 0x04u

/* Bity MADCTL */
#define MADCTL_MY 0x80u
#define MADCTL_MX 0x40u
#define MADCTL_MV 0x20u
#define MADCTL_BGR 0x08u

/* Czasy z kart katalogowych ILI9341 / ST7789: impuls resetu, reset i SLPOUT / SLPIN */
#define RESET_PULSE_MS 10u
#define WAKE_MS 120u

#define LCD_DATA_MAX 15u

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[LCD_DATA_MAX];
} lcd_cmd_t;

typedef struct {
    const lcd_cmd_t *init; /**< Komendy układu po SLPOUT (zasilanie, gamma) */
    uint8_t init_len;
    uint16_t ram_w; /**< RAM układu, orientacja 0 */
    uint16_t ram_h;
    uint8_t madctl; /**< MADCTL orientacji 0 przed flagami modułu */
} lcd_panel_t;

static const lcd_cmd_t k_ili9341_init[] = {
    {0xCF, 3, {0x00, 0x83, 0x30}},             /* Power control B */
    {0xED, 4, {0x64, 0x03, 0x12, 0x81}},       /* Power on sequence */
    {0xE8, 3, {0x85, 0x01, 0x79}},             /* Driver timing control A */
    {0xCB, 5, {0x39, 0x2C, 0x00, 0x34, 0x02}}, /* Power control A */
    {0xF7, 1, {0x20}},                         /* Pump ratio */
    {0xEA, 2, {0x00, 0x00}},                   /* Driver timing control B */
    {0xC0, 1, {0x26}},                         /* Power control 1 */
    {0xC1, 1, {0x11}},                         /* Power control 2 */
    {0xC5, 2, {0x35, 0x3E}},                   /* VCOM control 1 */
    {0xC7, 1, {0xBE}},                         /* VCOM control 2 */
    {0xB1, 2, {0x00, 0x1B}},                   /* Frame rate 70 Hz */
    {0xF2, 1, {0x08}},                         /* 3 gamma off */
    {0x26, 1, {0x01}},                         /* Gamma curve 1 */
    {0xE0,
     15,
     {0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0x87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05,
      0x00}}, /* Positive gamma */
    {0xE1,
     15,
     {0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A,
      0x1F}},                            /* Negative gamma */
    {0xB7, 1, {0x07}},                   /* Entry mode */
    {0xB6, 4, {0x0A, 0x82, 0x27, 0x00}}, /* Display function control */
};

static const lcd_cmd_t k_st7789_init[] = {
    {0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}}, /* Porch */
    {0xB7, 1, {0x45}},                         /* Gate control */
    {0xBB, 1, {0x2B}},                         /* VCOM */
    {0xC0, 1, {0x2C}},                         /* LCM control */
    {0xC2, 2, {0x01, 0xFF}},                   /* VDV and VRH enable */
    {0xC3, 1, {0x11}},                         /* VRH */
    {0xC4, 1, {0x20}},                         /* VDV */
    {0xC6, 1, {0x0F}},                         /* Frame rate 60 Hz */
    {0xD0, 2, {0xA4, 0xA1}},                   /* Power control 1 */
    {0xE0,
     14,
     {0xD0, 0x00, 0x05, 0x0E, 0x15, 0x0D, 0x37, 0x43, 0x47, 0x09, 0x15, 0x12, 0x16,
      0x19}}, /* Positive gamma */
    {0xE1,
     14,
     {0xD0, 0x00, 0x05, 0x0D, 0x0C, 0x06, 0x2D, 0x44, 0x40, 0x0E, 0x1C, 0x18, 0x16,
      0x19}}, /* Negative gamma */
};

#define COUNT(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

/* MADCTL orientacji 0: ILI9341 jak Adafruit (MX), ST7789 jak esp_lcd (0) */
static const lcd_panel_t k_panels[] = {
    [SYS_DISPLAY_PANEL_ILI9341] = {k_ili9341_init, COUNT(k_ili9341_init), 240, 320, MADCTL_MX},
    [SYS_DISPLAY_PANEL_ST7789] = {k_st7789_init, COUNT(k_st7789_init), 240, 320, 0},
};

typedef struct {
    display_cfg_t cfg;
    const lcd_panel_t *panel;
    sys_spi_dev_t spi;
    sys_io_claim_t cs;
    sys_io_claim_t dc;
    sys_io_claim_t rst;
    sys_io_claim_t bl;
    uint8_t orientation; /**< Bieżąca orientacja (sys_display_orientation_e) */
    uint16_t brightness; /**< Jasność ustawiona przez użytkownika, 0 .. BRIGHTNESS_MAX */
    uint8_t id;          /**< ID urządzenia (do błędów) */
    volatile bool busy;  /**< FLUSH w toku: od kolejki do done z ISR */
    display_flush_done_f flush_done; /**< done użytkownika bieżącego FLUSH */
    void *flush_ctx;                 /**< done_ctx bieżącego FLUSH */
} lcd_t;
#define SYS_DEVICE_CTX lcd_t

static bool pin_none(sys_io_pin_t p)
{
    return p.device == SYS_IO_PIN_NONE_DEVICE;
}

static void wait_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* Komenda: DC niski, bajt komendy, DC wysoki, dane (gdy są). DC zostaje wysoki. */
static err_h cmd(lcd_t *c, uint8_t op, const uint8_t *data, size_t len)
{
    ERR_TRY(sys_io_claim_drive(&c->dc, false));
    ERR_TRY(sys_spi_write(&c->spi, &op, 1));
    ERR_TRY(sys_io_claim_drive(&c->dc, true));
    if (len > 0) {
        ERR_TRY(sys_spi_write(&c->spi, data, len));
    }
    return NULL;
}

/* Komenda z jednym bajtem danych. */
static err_h cmd_byte(lcd_t *c, uint8_t op, uint8_t value)
{
    return cmd(c, op, &value, 1);
}

/* PWM_DUTY podświetlenia (odwrócone przy BL_ACTIVE_LOW); bez pinu nic. */
static err_h backlight(lcd_t *c, uint16_t duty)
{
    if (pin_none(c->cfg.backlight)) {
        return NULL;
    }
    uint32_t d = duty;
    if ((c->cfg.flags & SYS_DISPLAY_FLAG_BL_ACTIVE_LOW) != 0) {
        d = SYS_DISPLAY_BRIGHTNESS_MAX - d;
    }
    return SYS_IO_WRITE(c->cfg.backlight, OP_PWM_DUTY, .duty = d);
}

/*
 * MADCTL: baza układu XOR bity obrotu XOR odbicia modułu, OR BGR. Obrót w kierunku jak
 * lv_display_set_rotation w LVGL (lv_lcd_generic_mipi): 90 = MV|MY, 180 = MX|MY, 270 = MV|MX.
 */
static uint8_t madctl(const lcd_t *c)
{
    static const uint8_t k_rotation[4] = {
        0,
        MADCTL_MV | MADCTL_MY,
        MADCTL_MY | MADCTL_MX,
        MADCTL_MV | MADCTL_MX,
    };
    const uint8_t flags = c->cfg.flags;
    uint8_t m = c->panel->madctl ^ k_rotation[c->orientation & 3u];
    m ^= (flags & SYS_DISPLAY_FLAG_MIRROR_X) != 0 ? MADCTL_MX : 0u;
    m ^= (flags & SYS_DISPLAY_FLAG_MIRROR_Y) != 0 ? MADCTL_MY : 0u;
    m |= (flags & SYS_DISPLAY_FLAG_BGR) != 0 ? MADCTL_BGR : 0u;
    return m;
}

/* Reset sprzętowy albo SWRESET, potem czas na start układu. */
static err_h panel_reset(lcd_t *c)
{
    if (pin_none(c->cfg.reset)) {
        ERR_TRY(cmd(c, CMD_SWRESET, NULL, 0));
    } else {
        ERR_TRY(sys_io_claim_drive(&c->rst, false));
        wait_ms(RESET_PULSE_MS);
        ERR_TRY(sys_io_claim_drive(&c->rst, true));
    }
    wait_ms(WAKE_MS);
    return NULL;
}

/* Pełna inicjalizacja: reset, SLPOUT, tabela układu, COLMOD, inwersja, MADCTL, DISPON. */
static err_h panel_init(lcd_t *c)
{
    ERR_TRY(panel_reset(c));
    ERR_TRY(cmd(c, CMD_SLPOUT, NULL, 0));
    wait_ms(WAKE_MS);
    for (uint8_t i = 0; i < c->panel->init_len; i++) {
        const lcd_cmd_t *k = &c->panel->init[i];
        ERR_TRY(cmd(c, k->cmd, k->data, k->len));
    }
    ERR_TRY(cmd_byte(c, CMD_COLMOD, COLMOD_RGB565));
    const bool invert = (c->cfg.flags & SYS_DISPLAY_FLAG_INVERT) != 0;
    ERR_TRY(cmd(c, invert ? CMD_INVON : CMD_INVOFF, NULL, 0));
    ERR_TRY(cmd_byte(c, CMD_MADCTL, madctl(c)));
    return cmd(c, CMD_DISPON, NULL, 0);
}

/* RESET: inicjalizacja z bieżącą orientacją i jasnością. */
OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET)
{
    (void)args;
    (void)out;
    c->busy = false;
    ERR_TRY(panel_init(c));
    return backlight(c, c->brightness);
}

/* SUSPEND: podświetlenie 0, DISPOFF, SLPIN. */
static err_h suspend(lcd_t *c)
{
    ERR_TRY(backlight(c, 0));
    ERR_TRY(cmd(c, CMD_DISPOFF, NULL, 0));
    ERR_TRY(cmd(c, CMD_SLPIN, NULL, 0));
    wait_ms(WAKE_MS);
    return NULL;
}

/* RESUME: SLPOUT, DISPON, jasność domyślna z konfiguracji. */
static err_h resume(lcd_t *c)
{
    ERR_TRY(cmd(c, CMD_SLPOUT, NULL, 0));
    wait_ms(WAKE_MS);
    ERR_TRY(cmd(c, CMD_DISPON, NULL, 0));
    c->brightness = c->cfg.brightness_default;
    return backlight(c, c->brightness);
}

/* Okno szkła z przesunięciem mieści się w RAM układu; dc jest wymagany. */
static bool cfg_fits(const display_cfg_t *cfg, const lcd_panel_t *p)
{
    return !pin_none(cfg->dc) && cfg->width > 0 && cfg->height > 0 &&
           (uint32_t)cfg->offset_x + cfg->width <= p->ram_w &&
           (uint32_t)cfg->offset_y + cfg->height <= p->ram_h;
}

/* Piny: cs jako peryferium, dc i reset jako wyjścia (wysokie), podświetlenie jako PWM. */
static err_h claim_pins(uint8_t id, lcd_t *c, const display_cfg_t *cfg)
{
    ERR_TRY(sys_io_claim_peripheral(id, &c->cs, cfg->cs));
    ERR_TRY(sys_io_claim_output(id, &c->dc, cfg->dc, true));
    ERR_TRY(sys_io_claim_output(id, &c->rst, cfg->reset, true));
    return sys_io_claim(id, &c->bl, cfg->backlight, SYS_IO_MODE_PWM, 0, 0);
}

/* CREATE: sprawdzenie konfiguracji, piny, dołączenie do magistrali, inicjalizacja panelu. */
static err_h create(uint8_t id, lcd_t *c, const display_cfg_t *cfg)
{
    ERR_CHECK_RANGE(cfg->panel, 0, COUNT(k_panels) - 1);
    const lcd_panel_t *p = &k_panels[cfg->panel];
    ERR_CHECK(cfg_fits(cfg, p), ERR_BAD_ARG);
    c->cfg = *cfg;
    c->panel = p;
    c->orientation = cfg->orientation;
    c->brightness = cfg->brightness_default;
    c->id = id;
    c->busy = false;
    ERR_TRY(claim_pins(id, c, cfg));
    const spi_attach_args_t attach = {
        .cs = pin_none(cfg->cs) ? SYS_SPI_CS_NONE : cfg->cs.pin,
        .mode = cfg->spi_mode,
        .queue_depth = 1,
        .clock_Hz = cfg->freq_hz,
    };
    ERR_TRY(sys_spi_dev_attach(id, &c->spi, cfg->bus, &attach));
    ERR_TRY(panel_init(c));
    return backlight(c, c->brightness);
}

/* Wymiary szkła w bieżącej orientacji (przy 90 i 270 zamienione). */
static void size_now(const lcd_t *c, uint16_t *w, uint16_t *h)
{
    const bool turned = (c->orientation & 1u) != 0;
    *w = turned ? c->cfg.height : c->cfg.width;
    *h = turned ? c->cfg.width : c->cfg.height;
}

/* GEOMETRY: wymiary w bieżącej orientacji (przy 90 i 270 zamienione). */
OP_DECLARE_READ(CONTRACT_DISPLAY, OP_GEOMETRY)
{
    uint16_t w;
    uint16_t h;
    size_now(c, &w, &h);
    return CMD_REPLY(CONTRACT_DISPLAY, OP_GEOMETRY, .width = w, .height = h,
                     .format = SYS_DISPLAY_FORMAT_RGB565_SWAPPED,
                     .flush_max = CONFIG_DEVICE_SPI_LCD_FLUSH_MAX);
}

/* ORIENTATION, odczyt: bieżąca orientacja. */
OP_DECLARE_READ(CONTRACT_DISPLAY, OP_ORIENTATION)
{
    return CMD_REPLY(CONTRACT_DISPLAY, OP_ORIENTATION, .orientation = c->orientation);
}

/* ORIENTATION, zapis: zakres 0..3, zapamiętanie i MADCTL dla nowej orientacji. */
OP_DECLARE_WRITE(CONTRACT_DISPLAY, OP_ORIENTATION)
{
    ERR_CHECK_RANGE(args->orientation, 0, SYS_DISPLAY_ORIENTATION_270);
    if (c->busy) {
        return ERR_NEW(ERR_DISPLAY_FLUSH_BUSY, .id = c->id);
    }
    c->orientation = args->orientation;
    return cmd_byte(c, CMD_MADCTL, madctl(c));
}

/* BACKLIGHT, odczyt: jasność ustawiona przez użytkownika (bez odwrócenia przy BL_ACTIVE_LOW). */
OP_DECLARE_READ(CONTRACT_DISPLAY, OP_BACKLIGHT)
{
    return CMD_REPLY(CONTRACT_DISPLAY, OP_BACKLIGHT, .brightness_duty = c->brightness);
}

/* BACKLIGHT, zapis: zakres 0..SYS_DISPLAY_BRIGHTNESS_MAX, zapamiętanie i PWM_DUTY. */
OP_DECLARE_WRITE(CONTRACT_DISPLAY, OP_BACKLIGHT)
{
    ERR_CHECK_RANGE(args->brightness_duty, 0, SYS_DISPLAY_BRIGHTNESS_MAX);
    c->brightness = args->brightness_duty;
    return backlight(c, c->brightness);
}

/* RDDPM: DC niski, komenda i odczyt jednego bajtu odpowiedzi. Przy błędzie DC zostaje niski;
 * następna komenda sama ustawia DC. */
static err_h read_power_mode(lcd_t *c, uint8_t *mode)
{
    const uint8_t op = CMD_RDDPM;
    ERR_TRY(sys_io_claim_drive(&c->dc, false));
    ERR_TRY(sys_spi_write_read(&c->spi, &op, 1, mode, 1));
    return sys_io_claim_drive(&c->dc, true);
}

/* Stan z odpowiedzi RDDPM: same zera albo jedynki - brak odpowiedzi, SLPOUT i DISPON - działa. */
static uint8_t health_of(uint8_t mode)
{
    const uint8_t awake = RDDPM_SLPOUT | RDDPM_DISPON;
    if (mode == 0x00u || mode == 0xFFu) {
        return SYS_DISPLAY_HEALTH_NO_ANSWER;
    }
    return (mode & awake) == awake ? SYS_DISPLAY_HEALTH_OK : SYS_DISPLAY_HEALTH_RESTARTED;
}

/* HEALTH: bez FLAG_READABLE nieznany; inaczej z RDDPM. */
OP_DECLARE_READ(CONTRACT_DISPLAY, OP_HEALTH)
{
    uint8_t mode = 0;
    uint8_t health = SYS_DISPLAY_HEALTH_UNKNOWN;
    if ((c->cfg.flags & SYS_DISPLAY_FLAG_READABLE) != 0) {
        ERR_TRY(read_power_mode(c, &mode));
        health = health_of(mode);
    }
    return CMD_REPLY(CONTRACT_DISPLAY, OP_HEALTH, .health = health, .power_mode = mode);
}

/* Okno szkła ma sens w bieżącej orientacji, a len to jego bajty (RGB565) w granicach flush_max. */
static bool window_ok(const lcd_t *c, const display_flush_args_t *a)
{
    const display_window_t *w = &a->window;
    uint16_t width;
    uint16_t height;
    size_now(c, &width, &height);
    if (w->x1 > w->x2 || w->y1 > w->y2 || w->x2 >= width || w->y2 >= height) {
        return false;
    }
    const uint32_t bytes = (uint32_t)(w->x2 - w->x1 + 1) * (uint32_t)(w->y2 - w->y1 + 1) * 2u;
    return a->len == bytes && a->len <= CONFIG_DEVICE_SPI_LCD_FLUSH_MAX;
}

/* Przesunięcie szkła w RAM układu: 0 jak w cfg, 180 od drugiego brzegu, 90 i 270 z zamianą osi. */
static void ram_offset(const lcd_t *c, uint16_t *ox, uint16_t *oy)
{
    const display_cfg_t *g = &c->cfg;
    switch (c->orientation & 3u) {
    case SYS_DISPLAY_ORIENTATION_180:
        *ox = (uint16_t)(c->panel->ram_w - g->offset_x - g->width);
        *oy = (uint16_t)(c->panel->ram_h - g->offset_y - g->height);
        break;
    case SYS_DISPLAY_ORIENTATION_90:
    case SYS_DISPLAY_ORIENTATION_270:
        *ox = g->offset_y;
        *oy = g->offset_x;
        break;
    default:
        *ox = g->offset_x;
        *oy = g->offset_y;
        break;
    }
}

/* CASET albo RASET: początek i koniec (big endian). */
static err_h set_range(lcd_t *c, uint8_t op, uint16_t from, uint16_t to)
{
    const uint8_t data[4] = {(uint8_t)(from >> 8), (uint8_t)from, (uint8_t)(to >> 8), (uint8_t)to};
    return cmd(c, op, data, sizeof(data));
}

/* Koniec transferu (ISR): kończy FLUSH i woła done użytkownika; po RESET (busy zdjęte) nic.
 * Ograniczenie: stare done po RESET i nowym FLUSH zakończyłoby ten nowy (brak licznika generacji).
 */
static void flush_done_isr(void *ctx)
{
    lcd_t *c = ctx;
    if (!c->busy) {
        return;
    }
    const display_flush_done_f done = c->flush_done;
    void *done_ctx = c->flush_ctx;
    c->busy = false;
    done(done_ctx);
}

/* FLUSH: okno w RAM (CASET, RASET, RAMWR) i jedna transakcja z pikselami. */
OP_DECLARE_WRITE(CONTRACT_DISPLAY, OP_FLUSH)
{
    (void)out;
    ERR_CHECK(args->pixels != NULL && args->done != NULL, ERR_BAD_ARG);
    const display_window_t *w = &args->window;
    if (!window_ok(c, args)) {
        return ERR_NEW(ERR_DISPLAY_BAD_WINDOW, .id = c->id, .x1 = w->x1, .y1 = w->y1, .x2 = w->x2,
                       .y2 = w->y2);
    }
    if (c->busy) {
        return ERR_NEW(ERR_DISPLAY_FLUSH_BUSY, .id = c->id);
    }
    uint16_t ox;
    uint16_t oy;
    ram_offset(c, &ox, &oy);
    ERR_TRY(set_range(c, CMD_CASET, (uint16_t)(ox + w->x1), (uint16_t)(ox + w->x2)));
    ERR_TRY(set_range(c, CMD_RASET, (uint16_t)(oy + w->y1), (uint16_t)(oy + w->y2)));
    ERR_TRY(cmd(c, CMD_RAMWR, NULL, 0));
    c->flush_done = args->done;
    c->flush_ctx = args->done_ctx;
    c->busy = true;
    const spi_xfer_t xfer = {
        .tx = args->pixels, .tx_len = args->len, .done = flush_done_isr, .ctx = c};
    err_h e = sys_spi_queue(&c->spi, &xfer);
    if (e != NULL) {
        c->busy = false;
    }
    return e;
}

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET), OP_ENTRY_R(CONTRACT_DISPLAY, OP_GEOMETRY),
    OP_ENTRY(CONTRACT_DISPLAY, OP_BACKLIGHT),  OP_ENTRY(CONTRACT_DISPLAY, OP_ORIENTATION),
    OP_ENTRY_R(CONTRACT_DISPLAY, OP_HEALTH),   OP_ENTRY_W(CONTRACT_DISPLAY, OP_FLUSH),
};

SYS_DEVICE_TYPE_DEFINE(DEVICE_SPI_LCD, display_cfg_t, create, suspend, resume, k_ops)
