#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "runit.h"

void app_main(void) {
  runit_start();
  while (1) {
    vTaskDelay(portMAX_DELAY);
  }
}
