#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "runit.h"

#define TAG __FILE_NAME__

void app_main(void) {
  err_h err = runit_start();
  if (err != NULL) {
    ESP_LOGE(TAG, "runIT boot: a step failed, the board runs with the rest");
    err_report(err);
  }
  while (1) {
    vTaskDelay(portMAX_DELAY);
  }
}
