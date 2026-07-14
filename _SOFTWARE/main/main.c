#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "runit.h"

#define TAG __FILE_NAME__

void app_main(void) {
  runit_start();
  while (1) {
    vTaskDelay(portMAX_DELAY);
  }
}