
#include "esp_log.h"
#include "rik_shared.h"
#include "manager_ble.h"
#include "status.h"

#define TAG __FILE_NAME__


typedef __packed struct{
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
        if (rik_buff_tx) {
            m_ble_tx_enqueue(rik_buff_tx, (const uint8_t *)buf, len, true);
        }
    }
    if (rik_log_cfg_pkt.mirror_on_serial) {
        return vprintf(fmt, args);
    }
    return 0;
}


status_rep_t rik_parse_log_cfg(const uint8_t *packet_data, const uint16_t packet_len){
    if(packet_len != sizeof(rik_log_cfg_pkt_t)){
        
    } 
    return STA_OK;
}





