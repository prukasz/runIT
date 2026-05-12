#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "rik_main.h"

#define TAG __FILE_NAME__

void app_main(void) {
    ESP_LOGI(TAG, "=== RIK ===");

    rik_start();

    ESP_LOGI(TAG, "RIK Started");

    while (1) {
        vTaskDelay(portMAX_DELAY);
    }

}