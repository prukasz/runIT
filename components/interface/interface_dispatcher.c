#include "interface_dispatcher.h"
#include "interface_commands.h"

#define TAG __FILENAME__

/*****************************************************************************************/
TaskHandle_t interface_task_handle = NULL;

static interface_cfg_t *cfg = NULL;

static RingbufHandle_t interface_rx_buffer = NULL;

/*****************************************************************************************/


esp_err_t interface_parse_cmd_dev_cfg(const uint8_t *packet_data, const uint16_t packet_len){
    ESP_LOGI(TAG, "Parsing device config command with data length: %u", (unsigned)packet_len);

    uint8_t dev_type = packet_data[0]; /* Assuming first byte indicates device type */
    interface_dev_cfg_func cfg_setter = interface_dev_cfg_setter_table[dev_type];
    if(cfg_setter == NULL){
        ESP_LOGW(TAG, "No config setter found for device type: %u", dev_type);
        return ESP_OK;
    }
    esp_err_t res = cfg_setter(packet_data, packet_len);
    return res;
}

static void _interface_dispatch_cmd(uint8_t *cmd_data, size_t cmd_len){
    uint16_t packet_len = cmd_len-1; /*skip header*/
    uint8_t *packet_data = cmd_data+1; /*skip header*/
    
    parse_dispatch_table[cmd_data[0]](packet_data, packet_len);
}

void interface_buff_register_rx(RingbufHandle_t rx_buffer) {
    interface_rx_buffer = rx_buffer;
    ESP_LOGI(TAG, "Registered RX buffer with interface dispatcher");
}



static void interface_task(void* pvParameters){
        while (1) {

        xEventGroupWaitBits(
            cfg->connection_events,
            cfg->connection_bits_rx,  
            pdTRUE,  
            pdFALSE,
            portMAX_DELAY
        );
        
        uint8_t cmd_data[512];
        size_t cmd_len = sizeof(cmd_data);
        
        esp_err_t ret = interface_rx_dequeue(cmd_data, &cmd_len);
        if (ret == ESP_OK && cmd_len > 0) {
            _interface_dispatch_cmd(cmd_data, cmd_len);
        }

    }
}

void interface_init(interface_cfg_t *config){
    cfg = config;
    xTaskCreate(&interface_task, "interface_task", cfg->task_stack_size, NULL, cfg->task_priority, &interface_task_handle);
    ESP_LOGI(TAG, "Interface dispatcher initialized successfully");
}



static esp_err_t _interface_rb_dequeue(RingbufHandle_t rb, uint8_t *data, size_t *len) {
    size_t item_size = 0;
    void *item = xRingbufferReceive(rb, &item_size, 0);
    if (item == NULL) { return ESP_ERR_NOT_FOUND; }

    if (item_size > *len) {
        vRingbufferReturnItem(rb, item);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(data, item, item_size);
    *len = item_size;

    vRingbufferReturnItem(rb, item);
    return ESP_OK;
}

esp_err_t interface_rx_dequeue(uint8_t* data, size_t* len) {
    if (interface_rx_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return _interface_rb_dequeue(interface_rx_buffer, data, len);
}



TaskHandle_t interface_get_task_handle(){
    return interface_task_handle;
}