#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "board_time.h"
#include "errors.h"
#include "router_queue.h"

#define TAG __FILE_NAME__

void app_main(void) {
  ESP_LOGI(TAG, "runIT rework: up, uptime %llu ms", (unsigned long long)board_time_now_ms());
  err_h err = router_init();
  if (err != NULL) {
    err_report(err);
  }
  while (1) {
    vTaskDelay(portMAX_DELAY);
  }
}
