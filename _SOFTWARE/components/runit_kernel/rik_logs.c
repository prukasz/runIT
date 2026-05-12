
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
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    if (len > 0) {
        RIK_TX_NO_WAIT(buf, len);
    }
    if (rik_log_cfg_pkt.mirror_on_serial) {
        return vprintf(fmt, args);
    }
    return 0;
}
void rik_log_remote_enable(bool enable){
    rik_log_cfg_pkt.enable_stream = enable;
    rik_log_cfg_pkt.mirror_on_serial = enable; // For simplicity, mirror to serial when streaming is enabled
    rik_log_cfg_pkt.esp_log_level = ESP_LOG_WARN; // Default log level,
}


status_rep_t rik_parse_log_cfg(const uint8_t *packet_data, const uint16_t packet_len){
    if(packet_len != sizeof(rik_log_cfg_pkt_t)){
        
    } 
    return STA_OK;
}





