/**
 * @file
 * @brief Magistrala SPI ESP32 / ESP32-S3 jako urządzenie, typ 0x004F: kontrakt SPI (sys_spi_contract) na
 *        driver/spi_master.h ESP-IDF. Jedno urządzenie to jeden kontroler (SPI2 albo SPI3).
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/spi_master.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "sys_device_driver.h"
#include "sys_io_driver.h"
#include "sys_spi_contract/sys_spi_contract.h"

SYS_DEVICE_TYPE_DECLARE(DEVICE_ESP32_SPI, 0x004F)

#define SLOTS CONFIG_DEVICE_ESP32_SPI_SLOTS
#define QUEUE_MAX CONFIG_DEVICE_ESP32_SPI_QUEUE_MAX
#define BOUNCE CONFIG_DEVICE_ESP32_SPI_BOUNCE_BYTES

/** @brief Struktura CREATE (format na drucie) */
typedef struct __attribute__((packed)) {
    uint8_t host;          /**< 2: SPI2, 3: SPI3 */
    sys_io_pin_t sclk;     /**< Pin ESP32 */
    sys_io_pin_t mosi;     /**< SYS_IO_PIN_NONE: brak */
    sys_io_pin_t miso;     /**< SYS_IO_PIN_NONE: brak */
    uint32_t max_transfer; /**< Największy transfer w bajtach */
} device_esp32_spi_cfg_t;

/* Transakcja ESP-IDF i callbacki układu; `t.user` wskazuje na nią (trampoliny ISR). */
typedef struct {
    spi_transaction_t t;
    sys_spi_cb_f pre;
    sys_spi_cb_f done;
    void *ctx;
} trans_t;

/* Układ na magistrali. used i owner NONE: odłączony po czasie, czeka na pustą kolejkę. */
typedef struct {
    uint8_t used;
    uint8_t owner;
    uint8_t cs;
    uint8_t depth;
    uint8_t queued;
    uint8_t head;
    spi_device_handle_t h;
    trans_t q[QUEUE_MAX];
} chip_t;

typedef struct {
    uint8_t id;
    uint8_t ready;
    spi_host_device_t host;
    uint32_t max_transfer;
    sys_io_claim_t sclk;
    sys_io_claim_t mosi;
    sys_io_claim_t miso;
    sys_device_res_t bus_res;
    chip_t chip[SLOTS];
    uint8_t bounce_tx[BOUNCE]; /* transfer sekwencyjny z odczytem: tx, potem 0xFF */
    uint8_t bounce_rx[BOUNCE];
} esp_spi_t;

#define SYS_DEVICE_CTX esp_spi_t

static int gpio_of(sys_io_pin_t p)
{
    return p.device == SYS_IO_PIN_NONE_DEVICE ? -1 : (int)p.pin;
}

/* ISR: callbacki układu z transakcji. */
static void pre_isr(spi_transaction_t *t)
{
    const trans_t *x = t->user;
    if (x != NULL && x->pre != NULL) {
        x->pre(x->ctx);
    }
}

static void post_isr(spi_transaction_t *t)
{
    const trans_t *x = t->user;
    if (x != NULL && x->done != NULL) {
        x->done(x->ctx);
    }
}

/* Odbiera zakończone transakcje; false, gdy kolejka nie opróżniła się w `ticks`. */
static bool drain(chip_t *ch, TickType_t ticks)
{
    while (ch->queued > 0) {
        spi_transaction_t *t = NULL;
        if (spi_device_get_trans_result(ch->h, &t, ticks) != ESP_OK) {
            return false;
        }
        ch->queued--;
        ch->head = (uint8_t)((ch->head + 1u) % ch->depth);
    }
    return true;
}

static err_h remove_chip(esp_spi_t *c, chip_t *ch)
{
    esp_err_t rc = spi_bus_remove_device(ch->h);
    const uint8_t cs = ch->cs;
    memset(ch, 0, sizeof(*ch)); /* wolne także po błędzie */
    return rc == ESP_OK ? NULL : ERR_WRAP(ERR_ESP(rc), ERR_SPI_DEV_REMOVE, .bus = c->id, .cs = cs);
}

/* Usuwa układy odłączone po czasie, których kolejka już się opróżniła. */
static void purge(esp_spi_t *c)
{
    for (unsigned i = 0; i < SLOTS; i++) {
        chip_t *ch = &c->chip[i];
        if (ch->used && ch->owner == SYS_DEVICE_ID_NONE && drain(ch, 0)) {
            err_report(remove_chip(c, ch));
        }
    }
}

static chip_t *find(esp_spi_t *c, uint8_t cs)
{
    for (unsigned i = 0; i < SLOTS; i++) {
        if (c->chip[i].used && c->chip[i].cs == cs) {
            return &c->chip[i];
        }
    }
    return NULL;
}

static chip_t *free_slot(esp_spi_t *c)
{
    for (unsigned i = 0; i < SLOTS; i++) {
        if (!c->chip[i].used) {
            return &c->chip[i];
        }
    }
    return NULL;
}

/* Układ podłączony i należący do wołającego. */
static err_h owned(esp_spi_t *c, uint8_t cs, chip_t **out)
{
    purge(c);
    chip_t *ch = find(c, cs);
    ERR_CHECK(ch != NULL && ch->owner != SYS_DEVICE_ID_NONE, ERR_SPI_DEV_NOT_ADDED, .bus = c->id,
              .cs = cs);
    ERR_CHECK(ch->owner == sys_device_caller(), ERR_SPI_NOT_OWNER, .bus = c->id, .cs = cs,
              .owner = ch->owner);
    *out = ch;
    return NULL;
}

/*******************************************************************************
 * CONTRACT_SPI : OP_ATTACH
 ******************************************************************************/
/* ATTACH: konfiguracja układu i wolny cs (owner NONE: układ odłączony po czasie, jeszcze
 * nieusunięty) */
static err_h check_attach(esp_spi_t *c, const spi_attach_args_t *a)
{
    const bool cfg_ok = a->mode <= 3 && a->clock_Hz > 0 && a->queue_depth > 0;
    ERR_CHECK(cfg_ok && a->queue_depth <= QUEUE_MAX, ERR_SPI_BAD_DEV_CFG, .bus = c->id,
              .cs = a->cs);
    purge(c);
    const chip_t *taken = find(c, a->cs);
    if (taken != NULL) {
        return ERR_NEW(ERR_SPI_CS_TAKEN, .bus = c->id, .cs = a->cs, .owner = taken->owner);
    }
    return NULL;
}

OP_DECLARE_WRITE(CONTRACT_SPI, OP_ATTACH)
{
    ERR_TRY(check_attach(c, args));
    chip_t *ch = free_slot(c);
    ERR_CHECK(ch != NULL, ERR_SPI_NO_SLOT, .bus = c->id, .cs = args->cs, .slots = SLOTS);
    const spi_device_interface_config_t cfg = {
        .mode = args->mode,
        .clock_speed_hz = (int)args->clock_Hz,
        .spics_io_num = args->cs == SYS_SPI_CS_NONE ? -1 : (int)args->cs,
        .queue_size = args->queue_depth,
        .pre_cb = pre_isr,
        .post_cb = post_isr,
    };
    spi_device_handle_t h = NULL;
    esp_err_t rc = spi_bus_add_device(c->host, &cfg, &h);
    if (rc != ESP_OK) {
        return ERR_WRAP(ERR_ESP(rc), ERR_SPI_DEV_ADD, .bus = c->id, .cs = args->cs);
    }
    *ch = (chip_t){.used = 1,
                   .owner = sys_device_caller(),
                   .cs = args->cs,
                   .depth = args->queue_depth,
                   .h = h};
    return NULL;
}

/*******************************************************************************
 * CONTRACT_SPI : OP_DETACH
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_SPI, OP_DETACH)
{
    chip_t *ch = NULL;
    ERR_TRY(owned(c, args->cs, &ch));
    if (!drain(ch, pdMS_TO_TICKS(SYS_SPI_WAIT_MAX_MS))) {
        ch->owner = SYS_DEVICE_ID_NONE; /* cs bez właściciela; usunięcie po pustej kolejce */
        return ERR_NEW(ERR_SPI_TIMEOUT, .bus = c->id, .cs = args->cs,
                       .timeout_ms = SYS_SPI_WAIT_MAX_MS);
    }
    return remove_chip(c, ch);
}

/*******************************************************************************
 * CONTRACT_SPI : OP_XFER
 ******************************************************************************/
/* Transfer: flagi, bufory i długość; sekwencyjny z odczytem tylko blokująco, przez bufor pośredni
 */
static err_h check_xfer(const esp_spi_t *c, const spi_xfer_t *x, bool queued)
{
    const bool duplex = (x->flags & SYS_SPI_XFER_DUPLEX) != 0;
    ERR_CHECK((x->flags & ~SYS_SPI_XFER_DUPLEX) == 0, ERR_BAD_ARG);
    ERR_CHECK(x->tx != NULL || x->rx != NULL, ERR_BAD_ARG);
    ERR_CHECK(!duplex || (x->tx != NULL && x->rx != NULL && x->tx_len == x->rx_len), ERR_BAD_ARG);
    const size_t len = duplex ? x->tx_len : x->tx_len + x->rx_len;
    ERR_CHECK(len <= c->max_transfer, ERR_SPI_TOO_LONG, .bus = c->id, .cs = x->cs,
              .len = (uint32_t)len, .max = c->max_transfer);
    if (!duplex && x->rx != NULL) {
        ERR_CHECK(!queued, ERR_BAD_ARG);
        ERR_CHECK(len <= BOUNCE, ERR_SPI_TOO_LONG, .bus = c->id, .cs = x->cs, .len = (uint32_t)len,
                  .max = BOUNCE);
    }
    return NULL;
}

/* Transakcja ESP-IDF z transferu; sekwencyjny z odczytem idzie przez bufor pośredni. */
static void fill_trans(esp_spi_t *c, trans_t *t, const spi_xfer_t *x)
{
    *t = (trans_t){.pre = x->pre, .done = x->done, .ctx = x->ctx};
    t->t.user = t;
    t->t.flags = SPI_TRANS_DMA_USE_PSRAM; /* aligned PSRAM buffers (LVGL draw bufs) go zero-copy */
    if ((x->flags & SYS_SPI_XFER_DUPLEX) != 0) {
        t->t.length = x->tx_len * 8u;
        t->t.rxlength = x->rx_len * 8u;
        t->t.tx_buffer = x->tx;
        t->t.rx_buffer = x->rx;
    } else if (x->rx == NULL) {
        t->t.length = x->tx_len * 8u;
        t->t.tx_buffer = x->tx;
    } else {
        if (x->tx_len > 0) {
            memcpy(c->bounce_tx, x->tx, x->tx_len);
        }
        memset(c->bounce_tx + x->tx_len, 0xFF, x->rx_len);
        t->t.length = (x->tx_len + x->rx_len) * 8u;
        t->t.rxlength = t->t.length;
        t->t.tx_buffer = c->bounce_tx;
        t->t.rx_buffer = c->bounce_rx;
    }
}

OP_DECLARE_WRITE(CONTRACT_SPI, OP_XFER)
{
    chip_t *ch = NULL;
    ERR_TRY(owned(c, args->cs, &ch));
    ERR_TRY(check_xfer(c, args, false));
    if (!drain(ch, pdMS_TO_TICKS(SYS_SPI_WAIT_MAX_MS))) {
        return ERR_NEW(ERR_SPI_TIMEOUT, .bus = c->id, .cs = args->cs,
                       .timeout_ms = SYS_SPI_WAIT_MAX_MS);
    }
    trans_t t;
    fill_trans(c, &t, args);
    esp_err_t rc = spi_device_polling_transmit(ch->h, &t.t);
    if (rc != ESP_OK) {
        return ERR_WRAP(ERR_ESP(rc), ERR_SPI_XFER, .bus = c->id, .cs = args->cs);
    }
    if (t.t.rx_buffer == c->bounce_rx) {
        memcpy(args->rx, c->bounce_rx + args->tx_len, args->rx_len);
    }
    return NULL;
}

/*******************************************************************************
 * CONTRACT_SPI : OP_QUEUE
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_SPI, OP_QUEUE)
{
    chip_t *ch = NULL;
    ERR_TRY(owned(c, args->cs, &ch));
    ERR_TRY(check_xfer(c, args, true));
    (void)drain(ch, 0); /* zakończone zwalniają miejsce */
    ERR_CHECK(ch->queued < ch->depth, ERR_SPI_QUEUE_FULL, .bus = c->id, .cs = args->cs,
              .depth = ch->depth);
    trans_t *x = &ch->q[(ch->head + ch->queued) % ch->depth];
    fill_trans(c, x, args);
    esp_err_t rc = spi_device_queue_trans(ch->h, &x->t, 0);
    if (rc != ESP_OK) {
        return ERR_WRAP(ERR_ESP(rc), ERR_SPI_XFER, .bus = c->id, .cs = args->cs);
    }
    ch->queued++;
    return NULL;
}

/* Zasób urządzenia: kontroler. Jego układy normalnie usunięto wcześniej. */
static err_h bus_release(void *arg)
{
    esp_spi_t *c = arg;
    if (!c->ready) {
        return NULL;
    }
    err_h err = NULL;
    for (unsigned i = 0; i < SLOTS; i++) {
        if (c->chip[i].used) {
            ERR_KEEP_FIRST(err, remove_chip(c, &c->chip[i]));
        }
    }
    ERR_KEEP_FIRST(err, ERR_ESP(spi_bus_free(c->host)));
    c->ready = 0;
    return err;
}

/* Magistrala nie ma wyjść do zabezpieczenia; operacje są ALWAYS. */
static err_h suspend(esp_spi_t *c)
{
    (void)c;
    return NULL;
}

static err_h resume(esp_spi_t *c)
{
    (void)c;
    return NULL;
}

static err_h create(uint8_t id, esp_spi_t *c, const device_esp32_spi_cfg_t *cfg)
{
    ERR_CHECK(cfg->host >= 2 && cfg->host <= SOC_SPI_PERIPH_NUM && cfg->max_transfer > 0,
              ERR_SPI_BAD_DEV_CFG, .bus = id, .cs = SYS_SPI_CS_NONE);
    c->id = id;
    c->host = (spi_host_device_t)(cfg->host - 1u); /* SPI2 -> SPI2_HOST (1) */
    c->max_transfer = cfg->max_transfer;
    ERR_TRY(sys_io_claim_peripheral(id, &c->sclk, cfg->sclk));
    ERR_TRY(sys_io_claim_peripheral(id, &c->mosi, cfg->mosi));
    ERR_TRY(sys_io_claim_peripheral(id, &c->miso, cfg->miso));
    ERR_TRY(sys_device_res_add(id, &c->bus_res, bus_release, c));
    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = gpio_of(cfg->mosi),
        .miso_io_num = gpio_of(cfg->miso),
        .sclk_io_num = gpio_of(cfg->sclk),
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)cfg->max_transfer,
    };
    esp_err_t rc = spi_bus_initialize(c->host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (rc != ESP_OK) {
        return ERR_WRAP(ERR_ESP(rc), ERR_SPI_BUS_INIT, .bus = id,
                        .sclk = (uint8_t)gpio_of(cfg->sclk), .mosi = (uint8_t)gpio_of(cfg->mosi),
                        .miso = (uint8_t)gpio_of(cfg->miso));
    }
    c->ready = 1;
    return NULL;
}

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SPI, OP_ATTACH),
    OP_ENTRY_W(CONTRACT_SPI, OP_DETACH),
    OP_ENTRY_W(CONTRACT_SPI, OP_XFER),
    OP_ENTRY_W(CONTRACT_SPI, OP_QUEUE),
};

SYS_DEVICE_TYPE_DEFINE(DEVICE_ESP32_SPI, device_esp32_spi_cfg_t, create, suspend, resume, k_ops)
