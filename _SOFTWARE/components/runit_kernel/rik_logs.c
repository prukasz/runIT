
#include "esp_log.h"
#include "rik_shared.h"
#include "rik_tx_rx.h"
#include "status.h"
#include "rik_logs.h"

#define TAG __FILE_NAME__


typedef struct __attribute__((packed)){
    bool enable_stream;
    bool mirror_on_serial;
    uint8_t esp_log_level;
    status_manager_log_cfg status_log_cfg;
}rik_log_cfg_pkt_t;

static rik_log_cfg_pkt_t rik_log_cfg_pkt;

/**
 * @brief Logs redirection to data stream
 */
int rik_log_vprintf(const char *fmt, va_list args) {
    char buf[512];
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(buf, sizeof(buf), fmt, args_copy);
    va_end(args_copy);
    if (len > 0) {
        RIK_TX_LOG_NO_WAIT(buf, len);
    }
    if (rik_log_cfg_pkt.mirror_on_serial) {
        return vprintf(fmt, args);
    }
    return 0;
}
void sys_log_set_level(esp_log_level_t level) {
    rik_log_cfg_pkt.esp_log_level = level;
    esp_log_level_set("*", level);
}

void sys_log_mirror_on_serial(bool enable) {
    rik_log_cfg_pkt.mirror_on_serial = enable;
}

void sys_log_remote_enable(bool enable){
    enable ? esp_log_set_vprintf(rik_log_vprintf) : esp_log_set_vprintf(vprintf);
    rik_log_cfg_pkt.enable_stream = enable;
    rik_log_cfg_pkt.mirror_on_serial = enable; // For simplicity, mirror to serial when streaming is enabled
    rik_log_cfg_pkt.esp_log_level = ESP_LOG_INFO; // Default log level
    esp_log_level_set("*", rik_log_cfg_pkt.esp_log_level);
}







