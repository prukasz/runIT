#include "interface_dispatcher.h"
#include "interface_commands.h"
#include "rik_tx_rx.h"
#include "rtos_utils.h"

#define TAG __FILENAME__

/*****************************************************************************************/
static interface_cfg_t *_cfg = NULL;

static RingbufHandle_t _interface_rx_buffer = NULL;
/*****************************************************************************************/

R_TASK_DEFINE(interface_task, 4096);

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
    uint8_t* packet_data = cmd_data+1; /*skip header*/
    ESP_LOGI(TAG, "Dispatching command with header: %u and data length: %u", cmd_data[0], (unsigned)packet_len);
    interface_parse_func parse_func = parse_dispatch_table[cmd_data[0]];
    if(parse_func == NULL){
        ESP_LOGW(TAG, "No parser found for command header: %u", cmd_data[0]);
        return;
    }
    parse_func(packet_data, packet_len);
}

void interface_buff_register_rx(RingbufHandle_t rx_buffer) {
    _interface_rx_buffer = rx_buffer;
    ESP_LOGI(TAG, "Registered RX buffer with interface dispatcher");
}


static void interface_task_func(void* pvParameters){
        while (1) {
        uint8_t cmd_data[527];
        size_t cmd_len = 0;
        status_rep_t ret = RIK_RX_WAIT(cmd_data, sizeof(cmd_data), &cmd_len);
        ESP_LOGI(TAG, "Received event to process interface command");
        if (ret.e_code == ESP_OK && cmd_len > 0) {
            _interface_dispatch_cmd(cmd_data, cmd_len);
        }
        else {
            ESP_LOGW(TAG, "%s", esp_err_to_name(ret.e_code));
        }
    }
}

void interface_init(interface_cfg_t *config){
    R_TASK_START_ON_CORE(interface_task, interface_task_func, NULL, 5, 0);
    ESP_LOGI(TAG, "Interface task started");
}

TaskHandle_t interface_get_task_handle(){
    return interface_task;
}
