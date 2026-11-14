#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "board_time.h"
#include "errors.h"
#include "router_queue.h"
#include "sys_device_policy.h"
#include "sys_event.h"
#include "sys_log.h"
#include "sys_nvs.h"
#include "sys_session.h"
#include "sys_stream.h"
#include "sys_stream_router.h"
#include "sys_system.h"
#include "sys_uart.h"
#include "sys_watch.h"

#define TAG __FILE_NAME__

/* The board's own providers: their ids are names the board gives them (SYS_DATA_PROVIDER.MD "The id"). */
#define BOARD_PROVIDER_UART_CONSOLE 0x01u  // the console's UART (and the USB-Serial-JTAG): a client's or the master's way
#define BOARD_PROVIDER_UART_LINK    0x02u  // the UART to another board (CONFIG_SYS_UART_LINK): a slave's way

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
  err_report(sys_uart_init(BOARD_PROVIDER_UART_CONSOLE, BOARD_PROVIDER_UART_LINK));
  err_report(sys_session_attach(BOARD_PROVIDER_UART_CONSOLE, SYS_SESSION_UP));
#if CONFIG_SYS_UART_LINK
  err_report(sys_session_attach(BOARD_PROVIDER_UART_LINK, SYS_SESSION_DOWN));
#endif
  while (1) {
    vTaskDelay(portMAX_DELAY);
  }
}
