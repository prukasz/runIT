#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "board_time.h"
#include "errors.h"
#include "router_queue.h"
#include "sys_data_provider.h"
#include "sys_uart.h"

#define TAG __FILE_NAME__

/* The board's own providers: their ids are names the board gives them (SYS_DATA_PROVIDER.MD "The id"). */
#define BOARD_PROVIDER_UART_CONSOLE 0x01u  // the console's UART (and the USB-Serial-JTAG)
#define BOARD_PROVIDER_UART_LINK    0x02u  // the UART to another board (CONFIG_SYS_UART_LINK)

void app_main(void) {
  ESP_LOGI(TAG, "runIT rework: up, uptime %llu ms", (unsigned long long)board_time_now_ms());
  err_report(router_init());
  err_report(sys_uart_init(BOARD_PROVIDER_UART_CONSOLE, BOARD_PROVIDER_UART_LINK));
  // Until a session takes the console, its frames are commands for this board (the command binding).
  err_report(sys_data_provider_bind_commands(BOARD_PROVIDER_UART_CONSOLE));
  while (1) {
    vTaskDelay(portMAX_DELAY);
  }
}
