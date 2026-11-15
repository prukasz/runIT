#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include "board_time.h"
#include "errors.h"
#include "router_queue.h"
#include "sys_data_provider.h"
#include "sdkconfig.h"
#include "sys_device.h"
#include "sys_device_policy.h"
#include "sys_event.h"
#include "sys_io.h"
#include "sys_log.h"
#include "sys_nvs.h"
#include "sys_session.h"
#include "sys_stream.h"
#include "sys_stream_router.h"
#include "sys_system.h"
#include "sys_uart.h"
#include "sys_uart_contract.h"
#include "sys_watch.h"

#define TAG __FILE_NAME__

/* The board's own system devices and providers: never removed (SYS_DEVICE_CREATE_SYSTEM, STATE_SYSTEM). */
#define BOARD_DEV_GPIO              0x00u
#define BOARD_DEV_UART_LINK         0x01u
#define BOARD_DEV_TYPE_ESP32S3_GPIO 0x0040u  // device_esp32s3_gpio (0-byte CREATE config)
#define BOARD_DEV_TYPE_ESP32S3_UART 0x004Du  // device_esp32s3_uart (board_uart_cfg_t)

#define BOARD_PROVIDER_UART_CONSOLE 0x01u  // the console's UART (and the USB-Serial-JTAG): a client's or the master's way
#define BOARD_PROVIDER_UART_LINK    0x02u  // the UART to another board (CONFIG_BOARD_UART_LINK): a slave's way

static err_h create_system_device(uint8_t dev_id, uint16_t type_id, const void* cfg, uint32_t cfg_len) {
  uint8_t frame[SYS_DEVICE_CREATE_HDR + 16u];
  ERR_CHECK(cfg_len <= sizeof(frame) - SYS_DEVICE_CREATE_HDR, ERR_CMD_BAD_LEN, .got = (uint16_t)cfg_len,
            .need = (uint16_t)(sizeof(frame) - SYS_DEVICE_CREATE_HDR));
  memcpy(frame, &type_id, sizeof(type_id));
  frame[sizeof(type_id)] = SYS_DEVICE_CREATE_SYSTEM;
  if (cfg_len > 0u) {
    memcpy(frame + SYS_DEVICE_CREATE_HDR, cfg, cfg_len);
  }
  return sys_device_write(dev_id, CONTRACT_SYS_DEVICE_OP_CREATE, frame, SYS_DEVICE_CREATE_HDR + cfg_len);
}

#if CONFIG_BOARD_UART_LINK
/* Wire CREATE config for device_esp32s3_uart (no direct header include across the device isolation boundary). */
typedef struct __attribute__((packed)) {
  uint8_t uart_num;
  uint8_t mode;
  uint32_t baud;
  uint8_t parity;
  uint8_t stop_bits;
  sys_io_pin_t tx;
  sys_io_pin_t rx;
  sys_io_pin_t rts;
  sys_io_pin_t cts;
} board_uart_cfg_t;

_Static_assert(sizeof(board_uart_cfg_t) == 16u, "ESP32S3_UART create cfg");

static sys_io_pin_t board_pin(int gpio) {
  return gpio >= 0 ? (sys_io_pin_t){.device = BOARD_DEV_GPIO, .pin = (uint8_t)gpio} : SYS_IO_PIN_NONE;
}

static err_h init_board_link(void) {
  const board_uart_cfg_t cfg = {
      .uart_num = CONFIG_BOARD_UART_LINK_PORT,
      .mode = SYS_UART_MODE_FRAMED,
      .baud = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
      .parity = SYS_UART_PARITY_NONE,
      .stop_bits = 1u,
      .tx = board_pin(CONFIG_BOARD_UART_LINK_TX_PIN),
      .rx = board_pin(CONFIG_BOARD_UART_LINK_RX_PIN),
      .rts = board_pin(CONFIG_BOARD_UART_LINK_RTS_PIN),
      .cts = board_pin(CONFIG_BOARD_UART_LINK_CTS_PIN),
  };
  ERR_TRY(create_system_device(BOARD_DEV_UART_LINK, BOARD_DEV_TYPE_ESP32S3_UART, &cfg, sizeof(cfg)));
  const uint8_t prov_create[] = {
      SYS_DATA_PROVIDER_TYPE_UART,
      SYS_DATA_PROVIDER_CREATE_SYSTEM,
      BOARD_DEV_UART_LINK,
  };  // [type u8][flags u8][uint8_t device]
  ERR_TRY(sys_data_provider_write(BOARD_PROVIDER_UART_LINK, CONTRACT_PROVIDER_OP_CREATE, prov_create,
                                  sizeof(prov_create)));
  return sys_session_attach(BOARD_PROVIDER_UART_LINK, SYS_SESSION_DOWN);
}
#endif

static void send_record(err_h chain, const cmd_req_t* cmd) {
  uint8_t rec[SYS_LOG_RECORD_MAX];
  sys_log_error(rec, cmd_err_record(cmd, chain, rec, sizeof(rec)));
}

static void error_sink(err_h chain, const cmd_req_t* cmd) {
  err_h response = sys_device_policy_handle(chain);
  send_record(chain, cmd);
  if (response != NULL) {
    err_log(response);
    send_record(response, NULL);
    err_free(response);
  }
}

static err_h on_notify_event(const sys_event_t* event, void* ctx) {
  (void)ctx;
  if (event != NULL && (event->flags & SYS_EVENT_NOTIFY) != 0u) {
    err_free(sys_stream_send(SYS_STREAM_EVENTS, event, (uint32_t)(sizeof(*event) + event->len)));
  }
  return NULL;
}

static err_h init_streams_and_log(void) {
  static const sys_stream_cfg_t k_streams[] = {
      {.id = SYS_STREAM_TELEMETRY, .flags = 0},
      {.id = SYS_STREAM_LOGS,      .flags = 0},
      {.id = SYS_STREAM_ERRORS,    .flags = SYS_STREAM_FLAG_URGENT},
      {.id = SYS_STREAM_EVENTS,    .flags = SYS_STREAM_FLAG_URGENT},
  };
  for (size_t i = 0; i < sizeof(k_streams) / sizeof(k_streams[0]); i++) {
    ERR_TRY(sys_stream_create(&k_streams[i]));
  }
  sys_stream_mark_system();
  ERR_TRY(sys_stream_init());
  ERR_TRY(sys_log_init());
  err_set_sink(error_sink);
  uint16_t handle = 0;
  return sys_event_subscribe(
      (sys_event_key_t){.tag = SYS_EVENT_ANY_NOTIFY, .source = SYS_EVENT_ANY_SOURCE, .channel = SYS_EVENT_ANY_CHANNEL},
      on_notify_event, NULL, SYS_EVENT_RUN_FIRST, &handle);
}

void app_main(void) {
  ESP_LOGI(TAG, "runIT rework: up, uptime %llu ms", (unsigned long long)board_time_now_ms());
  err_report(sys_nvs_init("nvs"));
  err_report(router_init());
  err_report(sys_event_init());
  err_report(sys_device_policy_init());
  err_report(sys_session_init());
  err_report(init_streams_and_log());
  err_report(sys_system_init());
  err_report(sys_watch_init());
  err_report(sys_uart_init(BOARD_PROVIDER_UART_CONSOLE));
  err_report(sys_session_attach(BOARD_PROVIDER_UART_CONSOLE, SYS_SESSION_UP));
  err_report(create_system_device(BOARD_DEV_GPIO, BOARD_DEV_TYPE_ESP32S3_GPIO, NULL, 0u));
#if CONFIG_BOARD_UART_LINK
  err_report(init_board_link());
#endif
  while (1) {
    vTaskDelay(portMAX_DELAY);
  }
}
