#include "sys_flasher.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_flash.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_rom_md5.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "sys_flasher_rom.h"
#include "sys_session.h"
#include "sys_uart.h"
#include "utils.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_FLASHER, "flasher", ERR_SYS_FLASHER_MAP)

#define TAG "flasher"

#define ROM_BAUD        115200u                                   // the ROM bootloader's rate until CHANGE_BAUD
#define SYNC_TRIES      10u
#define SYNC_WAIT_MS    100u                                      // one SYNC's answer
#define ANSWER_WAIT_MS  3000u                                     // a command's answer
#define ERASE_MS_PER_MB 30000u                                    // FLASH_BEGIN erases the region first (esptool's figure)
#define MD5_MS_PER_MB   8000u
#define READ_IDLE_MS    5u                                        // a pause in the line ends a read
#define RESET_MS        100u                                      // EN held low
#define BOOT_HOLD_MS    50u                                       // BOOT still low after EN rises: the ROM reads it as it starts
#define SETTLE_MS       50u                                       // after a new rate, before the next command
#define TABLE_END       (CONFIG_PARTITION_TABLE_OFFSET + 0x1000u) // the bootloader and the partition table: 0x0 .. here
#define DATA_HEAD       16u                                       // FLASH_DATA: [size][seq][0][0] before the block
#define PACKET_MAX      (SYS_FLASHER_ROM_HEAD + DATA_HEAD + SYS_FLASHER_ROM_BLOCK)
#define ANSWER_MAX      96u                                       // the longest answer: SPI_FLASH_MD5's 32 hex digits
#define MB              (1024u * 1024u)
#define BOOT_TEXT_MS    300u                                      // what the ROM says as it starts, logged
#define FIRMWARE_SKIP_MS 1500u                                    // its ROM and bootloader first, at another rate: skipped
#define FIRMWARE_TEXT_MS 1500u                                    // then what its firmware says, logged

_Static_assert(GPIO_IS_VALID_OUTPUT_GPIO(CONFIG_SYS_FLASHER_EN_PIN), "the EN wire's pin must drive");
_Static_assert(GPIO_IS_VALID_OUTPUT_GPIO(CONFIG_SYS_FLASHER_BOOT_PIN), "the BOOT wire's pin must drive");

U_TASK_DEFINE(sys_flasher_task_h, CONFIG_SYS_FLASHER_TASK_STACK);

// What is written: copies of this board's flash, and the regions erased. The same partition table on both boards.
typedef struct {
  uint32_t app_from;    // this board's running app
  uint32_t app_to;      // ota_0
  uint32_t app_len;     // its image's length
  uint32_t nvs_at;
  uint32_t nvs_len;
  uint32_t otadata_at;
  uint32_t otadata_len;
  uint32_t flash_size;  // this board's: the other's flash is set to it
} plan_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static struct {
  uint8_t link;                                      // the link's provider
  sys_flasher_status_t status;                       // under s_lock
  plan_t plan;                                       // set by start, read by the task
  volatile bool abort;
  uint32_t heard;                                    // the task's: bytes the line brought since the link was lent
  uint8_t packet[PACKET_MAX];                        // the task's: a command before SLIP
  uint8_t line[SYS_FLASHER_SLIP_MAX(PACKET_MAX)];    // the task's: the command as it goes out
  uint8_t answer[ANSWER_MAX];                        // the task's: the answer being decoded
  uint8_t chunk[64];                                 // the task's: bytes read
} s_flasher;

/* ----------------------------------------------------------------------------
 * The wires and the progress
 * ------------------------------------------------------------------------- */

// Push-pull, always driven: a released wire sat low on the PCB's GPIO header (its protection pulls it down) and held the
// other board in reset, so high is driven, not left to its pull-up. Its RST and BOOT buttons are not used while wired.
static void wire(int pin, bool low) {
  (void)gpio_set_level((gpio_num_t)pin, low ? 0 : 1);
}

// BOOT low while EN rises: the ROM starts its download mode.
static void enter_rom(void) {
  wire(CONFIG_SYS_FLASHER_BOOT_PIN, true);
  wire(CONFIG_SYS_FLASHER_EN_PIN, true);
  vTaskDelay(MSEC(RESET_MS));
  wire(CONFIG_SYS_FLASHER_EN_PIN, false);
  vTaskDelay(MSEC(BOOT_HOLD_MS));
  wire(CONFIG_SYS_FLASHER_BOOT_PIN, false);
}

// BOOT high while EN rises: the board starts its firmware.
static void restart_board(void) {
  wire(CONFIG_SYS_FLASHER_BOOT_PIN, false);
  wire(CONFIG_SYS_FLASHER_EN_PIN, true);
  vTaskDelay(MSEC(RESET_MS));
  wire(CONFIG_SYS_FLASHER_EN_PIN, false);
}

static void set_step(uint8_t step) {
  portENTER_CRITICAL(&s_lock);
  s_flasher.status.step = step;
  portEXIT_CRITICAL(&s_lock);
}

static void add_done(uint32_t bytes) {
  portENTER_CRITICAL(&s_lock);
  s_flasher.status.done += bytes;
  portEXIT_CRITICAL(&s_lock);
}

/* ----------------------------------------------------------------------------
 * The ROM's commands (rom/sys_flasher_rom.h): the data is written at packet + SYS_FLASHER_ROM_HEAD
 * ------------------------------------------------------------------------- */

static uint8_t* data_at(void) {
  return s_flasher.packet + SYS_FLASHER_ROM_HEAD;
}

// Little-endian words as a command's data (the ESP32-S3 is little-endian).
static uint32_t words(const uint32_t* w, uint32_t n) {
  memcpy(data_at(), w, n * sizeof(uint32_t));
  return n * (uint32_t)sizeof(uint32_t);
}

// The bytes heard since the link was lent, for ERR_FLASHER_NO_ANSWER: none is a silent line (power, wires, EN).
static uint16_t heard(void) {
  return (uint16_t)(s_flasher.heard < UINT16_MAX ? s_flasher.heard : UINT16_MAX);
}

static uint32_t elapsed_ms(TickType_t since) {
  return (uint32_t)(xTaskGetTickCount() - since) * portTICK_PERIOD_MS;
}

// The answer to `op` within `wait_ms`; what comes before it (the ROM's text, another op's answer) is skipped.
static err_h wait_answer(uint8_t op, uint32_t wait_ms, sys_flasher_rom_answer_t* answer) {
  sys_flasher_slip_rx_t rx;
  sys_flasher_slip_start(&rx, s_flasher.answer, sizeof(s_flasher.answer));
  const TickType_t start = xTaskGetTickCount();
  while (elapsed_ms(start) < wait_ms) {
    uint32_t n = 0;
    ERR_TRY(sys_uart_read(s_flasher.link, s_flasher.chunk, sizeof(s_flasher.chunk), &n, wait_ms - elapsed_ms(start),
                          READ_IDLE_MS));
    s_flasher.heard += n;
    for (uint32_t i = 0; i < n; i++) {
      if (sys_flasher_slip_feed(&rx, s_flasher.chunk[i]) && sys_flasher_rom_parse(rx.buf, rx.len, answer) &&
          answer->op == op) {
        ERR_CHECK(answer->status == 0, ERR_FLASHER_ROM, .op = op, .code = answer->error);
        return NULL;
      }
    }
  }
  return ERR_NEW(ERR_FLASHER_NO_ANSWER, .op = op, .bytes = heard());
}

// One command, its data `len` bytes at data_at(), and its answer (NULL: only its status).
static err_h command(uint8_t op, uint32_t len, uint32_t checksum, uint32_t wait_ms, sys_flasher_rom_answer_t* answer) {
  ERR_TRY(sys_uart_flush_input(s_flasher.link));
  const uint32_t n = sys_flasher_rom_command(op, data_at(), len, checksum, s_flasher.packet);
  const uint32_t m = sys_flasher_slip_encode(s_flasher.packet, n, s_flasher.line);
  ERR_TRY(sys_uart_write(s_flasher.link, s_flasher.line, m, ANSWER_WAIT_MS));
  sys_flasher_rom_answer_t ignored;
  return wait_answer(op, wait_ms, answer != NULL ? answer : &ignored);
}

static uint32_t per_mb(uint32_t len, uint32_t ms) {
  const uint32_t t = (uint32_t)(((uint64_t)len * ms + MB - 1u) / MB);
  return t > ANSWER_WAIT_MS ? t : ANSWER_WAIT_MS;
}

/* What the board on the link says for `ms`, logged with `when`: its ROM's boot lines ("waiting for download" with BOOT
 * held), or its firmware's start (its bootloader's and runIT's log lines, at the link's rate). */
static void log_said(uint32_t ms, const char* when) {
  char text[160];
  uint32_t len = 0;
  const TickType_t start = xTaskGetTickCount();
  while (elapsed_ms(start) < ms && len < sizeof(text) - 1u) {
    uint32_t n = 0;
    if (sys_uart_read(s_flasher.link, (uint8_t*)text + len, sizeof(text) - 1u - len, &n, ms - elapsed_ms(start),
                      READ_IDLE_MS * 4u) != NULL) {
      break;
    }
    len += n;
  }
  s_flasher.heard += len;
  for (uint32_t i = 0; i < len; i++) {
    text[i] = (text[i] >= 0x20 && text[i] < 0x7F) ? text[i] : '.';
  }
  text[len] = 0;
  ESP_LOGI(TAG, "the board on the link said %lu B %s: %s", (unsigned long)len, when, text);
}

// SYNC until the ROM answers: it learns the rate from it.
static err_h sync_rom(void) {
  for (uint32_t i = 0; i < SYNC_TRIES && !s_flasher.abort; i++) {
    uint8_t* d = data_at();
    d[0] = 0x07;
    d[1] = 0x07;
    d[2] = 0x12;
    d[3] = 0x20;
    memset(d + 4, 0x55, 32);
    err_h err = command(SYS_FLASHER_ROM_SYNC, 36, 0, SYNC_WAIT_MS, NULL);
    if (err == NULL) {
      vTaskDelay(MSEC(SYNC_WAIT_MS));  // it answers a SYNC several times: the rest is flushed by the next command
      return NULL;
    }
    err_free(err);
  }
  ERR_CHECK(!s_flasher.abort, ERR_FLASHER_ABORTED);
  return ERR_NEW(ERR_FLASHER_NO_ANSWER, .op = SYS_FLASHER_ROM_SYNC, .bytes = heard());
}

// The ROM bootloader, at the flasher's rate, with its flash attached and sized.
static err_h connect_rom(void) {
  ERR_TRY(sys_uart_set_baud(s_flasher.link, ROM_BAUD));
  ERR_TRY(sys_uart_flush_input(s_flasher.link));
  s_flasher.heard = 0;
  // Driven high: a low read is something holding the line down (the other board unpowered, a short).
  ESP_LOGI(TAG, "the lines driven high: EN %d, BOOT %d", gpio_get_level((gpio_num_t)CONFIG_SYS_FLASHER_EN_PIN),
           gpio_get_level((gpio_num_t)CONFIG_SYS_FLASHER_BOOT_PIN));
  enter_rom();
  log_said(BOOT_TEXT_MS, "as its ROM started");
  ERR_TRY(sync_rom());
  ERR_TRY(command(SYS_FLASHER_ROM_CHANGE_BAUD, words((const uint32_t[]){CONFIG_SYS_FLASHER_BAUD, 0}, 2), 0,
                  ANSWER_WAIT_MS, NULL));
  ERR_TRY(sys_uart_set_baud(s_flasher.link, CONFIG_SYS_FLASHER_BAUD));
  vTaskDelay(MSEC(SETTLE_MS));
  ERR_TRY(command(SYS_FLASHER_ROM_SPI_ATTACH, words((const uint32_t[]){0, 0}, 2), 0, ANSWER_WAIT_MS, NULL));
  const uint32_t params[] = {0, s_flasher.plan.flash_size, 64u * 1024u, 4096u, 256u, 0xFFFFu};
  return command(SYS_FLASHER_ROM_SPI_PARAMS, words(params, ARRAY_LEN(params)), 0, ANSWER_WAIT_MS, NULL);
}

// FLASH_BEGIN: the ROM erases [at, at + len) first; `packets` FLASH_DATA follow.
static err_h flash_begin(uint32_t at, uint32_t len, uint32_t packets) {
  const uint32_t w[] = {len, packets, SYS_FLASHER_ROM_BLOCK, at, 0};
  return command(SYS_FLASHER_ROM_FLASH_BEGIN, words(w, ARRAY_LEN(w)), 0, per_mb(len, ERASE_MS_PER_MB), NULL);
}

// The other board's MD5 of [at, at + len) against `digest`.
static err_h verify(uint32_t at, uint32_t len, const uint8_t digest[ESP_ROM_MD5_DIGEST_LEN]) {
  sys_flasher_rom_answer_t answer;
  ERR_TRY(command(SYS_FLASHER_ROM_SPI_FLASH_MD5, words((const uint32_t[]){at, len, 0, 0}, 4), 0,
                  per_mb(len, MD5_MS_PER_MB), &answer));
  char hex[32];
  sys_flasher_md5_hex(digest, hex);
  ERR_CHECK(answer.len >= sizeof(hex) && memcmp(answer.data, hex, sizeof(hex)) == 0, ERR_FLASHER_VERIFY, .addr = at);
  return NULL;
}

// `len` bytes of this board's flash from `from` to the other board's `to`, block by block, then checked.
static err_h copy(uint32_t from, uint32_t to, uint32_t len) {
  const uint32_t packets = (len + SYS_FLASHER_ROM_BLOCK - 1u) / SYS_FLASHER_ROM_BLOCK;
  ERR_TRY(flash_begin(to, len, packets));
  md5_context_t md5;
  esp_rom_md5_init(&md5);
  uint8_t* block = data_at() + DATA_HEAD;
  for (uint32_t seq = 0; seq < packets; seq++) {
    ERR_CHECK(!s_flasher.abort, ERR_FLASHER_ABORTED);
    const uint32_t at = seq * SYS_FLASHER_ROM_BLOCK;
    const uint32_t n = len - at < SYS_FLASHER_ROM_BLOCK ? len - at : SYS_FLASHER_ROM_BLOCK;
    ERR_TRY_ESP(esp_flash_read(NULL, block, from + at, n));
    esp_rom_md5_update(&md5, block, n);
    memset(block + n, 0xFF, SYS_FLASHER_ROM_BLOCK - n);  // the last block is whole: erased flash stays erased
    const uint32_t head[] = {SYS_FLASHER_ROM_BLOCK, seq, 0, 0};
    memcpy(data_at(), head, sizeof(head));
    ERR_TRY(command(SYS_FLASHER_ROM_FLASH_DATA, DATA_HEAD + SYS_FLASHER_ROM_BLOCK,
                    sys_flasher_rom_checksum(block, SYS_FLASHER_ROM_BLOCK), ANSWER_WAIT_MS, NULL));
    add_done(n);
  }
  uint8_t digest[ESP_ROM_MD5_DIGEST_LEN];
  esp_rom_md5_final(digest, &md5);
  return verify(to, len, digest);
}

// What the other board gets, read before the link is lent.
static err_h make_plan(plan_t* plan) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* ota0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
  const esp_partition_t* nvs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
  const esp_partition_t* otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
  ERR_CHECK(running != NULL && ota0 != NULL && nvs != NULL && otadata != NULL, ERR_FLASHER_IMAGE);
  const esp_partition_pos_t pos = {.offset = running->address, .size = running->size};
  esp_image_metadata_t image;
  ERR_CHECK(esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &image) == ESP_OK, ERR_FLASHER_IMAGE);
  uint32_t flash_size = 0;
  ERR_CHECK(esp_flash_get_size(NULL, &flash_size) == ESP_OK, ERR_FLASHER_IMAGE);
  *plan = (plan_t){
      .app_from = running->address,
      .app_to = ota0->address,
      .app_len = image.image_len,
      .nvs_at = nvs->address,
      .nvs_len = nvs->size,
      .otadata_at = otadata->address,
      .otadata_len = otadata->size,
      .flash_size = flash_size,
  };
  return NULL;
}

/* ----------------------------------------------------------------------------
 * The job
 * ------------------------------------------------------------------------- */

static err_h program(const plan_t* plan) {
  set_step(SYS_FLASHER_STEP_CONNECT);
  ERR_TRY(connect_rom());
  set_step(SYS_FLASHER_STEP_BOOTLOADER);
  ERR_TRY(copy(0, 0, TABLE_END));
  set_step(SYS_FLASHER_STEP_ERASE);
  ERR_TRY(flash_begin(plan->nvs_at, plan->nvs_len, 0));  // the new board starts with id 0 and no settings
  ERR_TRY(flash_begin(plan->otadata_at, plan->otadata_len, 0));  // and boots ota_0
  set_step(SYS_FLASHER_STEP_APP);
  return copy(plan->app_from, plan->app_to, plan->app_len);
}

// The link lent, the board programmed and restarted (into what it has: runIT when it was written), the link back.
static err_h flash(void) {
  ERR_TRY(sys_uart_link_direct(true));
  err_h err = program(&s_flasher.plan);
  set_step(SYS_FLASHER_STEP_RESTART);
  restart_board();
  if (sys_uart_set_baud(s_flasher.link, CONFIG_SYS_UART_LINK_BAUD) == NULL) {
    vTaskDelay(MSEC(FIRMWARE_SKIP_MS));
    err_free(sys_uart_flush_input(s_flasher.link));
    // Its log text and its own hello (COBS frames show as dots); the link's session says hello again once it is back.
    log_said(FIRMWARE_TEXT_MS, "as its firmware ran");
  }
  ERR_KEEP_FIRST(err, sys_uart_link_direct(false));  // its session says hello; the new board's hello opens it
  return err;
}

static void flasher_task(void* arg) {
  (void)arg;
  while (true) {
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    err_h err = flash();
    portENTER_CRITICAL(&s_lock);
    s_flasher.status.state = err == NULL ? SYS_FLASHER_DONE : SYS_FLASHER_FAILED;
    s_flasher.status.error = err_root_tag(err);
    portEXIT_CRITICAL(&s_lock);
    err_report(err);
  }
}

/* ----------------------------------------------------------------------------
 * Use
 * ------------------------------------------------------------------------- */

err_h sys_flasher_init(uint8_t link_provider) {
  s_flasher.link = link_provider;
  const gpio_config_t io = {
      .pin_bit_mask = (1ULL << CONFIG_SYS_FLASHER_EN_PIN) | (1ULL << CONFIG_SYS_FLASHER_BOOT_PIN),
      .mode = GPIO_MODE_INPUT_OUTPUT,  // driven both ways, and read back: a driven high that reads low is a short or a hold
  };
  wire(CONFIG_SYS_FLASHER_EN_PIN, false);
  wire(CONFIG_SYS_FLASHER_BOOT_PIN, false);
  ERR_TRY_ESP(gpio_config(&io));
  if (sys_flasher_task_h == NULL) {
    U_TASK_START(sys_flasher_task_h, flasher_task, NULL, CONFIG_SYS_FLASHER_TASK_PRIO);
  }
  return NULL;
}

err_h sys_flasher_start(void) {
  ERR_CHECK(sys_session_get_board_id() != SYS_SESSION_ID_DEFAULT, ERR_FLASHER_BOARD_ID);
  plan_t plan;
  ERR_TRY(make_plan(&plan));
  portENTER_CRITICAL(&s_lock);
  const bool busy = s_flasher.status.state == SYS_FLASHER_RUNNING;
  if (!busy) {
    s_flasher.plan = plan;
    s_flasher.abort = false;
    s_flasher.status = (sys_flasher_status_t){
        .state = SYS_FLASHER_RUNNING,
        .step = SYS_FLASHER_STEP_CONNECT,
        .total = TABLE_END + plan.app_len,
    };
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(!busy, ERR_FLASHER_BUSY);
  xTaskNotifyGive(sys_flasher_task_h);
  return NULL;
}

void sys_flasher_abort(void) {
  s_flasher.abort = true;
}

sys_flasher_status_t sys_flasher_status(void) {
  portENTER_CRITICAL(&s_lock);
  const sys_flasher_status_t status = s_flasher.status;
  portEXIT_CRITICAL(&s_lock);
  return status;
}
