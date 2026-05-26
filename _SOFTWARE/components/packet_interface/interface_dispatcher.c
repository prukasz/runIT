#include "interface_dispatcher.h"
#include "interface_commands.h"
#include "rik_tx_rx.h"
#include "rtos_utils.h"


#define TAG __FILE_NAME__

/*****************************************************************************************/
static interface_cfg_t *_cfg = NULL;

static RingbufHandle_t _interface_rx_buffer = NULL;
/*****************************************************************************************/

static interface_parse_func parse_dispatch_table[256] = { 0 };

status_rep_t interface_register_parser(uint8_t header, interface_parse_func f){
    if(header >= 256) return STA_W(PWE_ERR_PARSE_NOT_FOUND, OWNER_RIK_DRIVER_INIT, header);
    parse_dispatch_table[header] = f;
    return STA_OK;
}

status_rep_t interface_unregister_parser(uint8_t header){
    if(header >= 256) return STA_W(PWE_ERR_PARSE_NOT_FOUND, OWNER_RIK_DRIVER_INIT, header);
    parse_dispatch_table[header] = NULL;
    return STA_OK;
}


R_TASK_DEFINE(interface_task, 4096);

void interface_buff_register_rx(RingbufHandle_t rx_buffer) {
    _interface_rx_buffer = rx_buffer;
    ESP_LOGI(TAG, "Registered RX buffer with interface dispatcher");
}


static void interface_task_func(void* pvParameters){
    (void)pvParameters;
    while (1) {
        uint8_t cmd_data[527];
        size_t cmd_len = 0;
        status_rep_t ret = RIK_RX_WAIT(cmd_data, sizeof(cmd_data), &cmd_len);
        ESP_LOGI(TAG, "Received event to process interface command");
        if (ret.e_code == ESP_OK && cmd_len > 0) {
            interface_parse_func parser = parse_dispatch_table[cmd_data[0]];
            if (parser) {
                STA_P(parser(cmd_data + 1, cmd_len - 1));
            } else {
                ESP_LOGW(TAG, "No parser for command header: 0x%02X", cmd_data[0]);
            }
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
