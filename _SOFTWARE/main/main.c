#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include "status.h"

#include "runit.h"

static const char *TAG = __FILE_NAME__;

void app_main(void) {
    ESP_LOGI(TAG, "=== System Core Started ===");

    runit_start();
    /* TODO: Call Status/Error handler Init */
    /* TODO: Call BLE Manager Init */

    ESP_LOGI(TAG, "Minimal initialization complete. Entering idle loop.");

    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}