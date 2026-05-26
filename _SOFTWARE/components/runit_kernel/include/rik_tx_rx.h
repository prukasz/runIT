#pragma once
#include "status.h"
#include "manager_ble.h"
#include "rik_shared.h"
#include "string.h"

/**
 * @brief send data via remote interface
 * @param data buffer with data
 * @param len len of data to send from buffer
 * @param interface if more than one aviable select, else 0
 * @return status_rep_t.e_code 0 on success, ERR_INTERFACE_UNAVAILABLE, ERR_TX_BUFFER_FULL, ESP_ERR_INVALID_ARG
 */
static __inline status_rep_t rik_tx_send(RingbufHandle_t buff, const uint8_t* data, size_t len, uint32_t interface, bool return_on_full){
    status_rep_t rep = STA_OK;
    if (!data || !len || !buff){ return STA_C(ESP_ERR_INVALID_ARG, OWNER_RIK_TX_SEND, 0);}
    if (interface == 0){
        if(_rik_ble_active){ 
            rep = STA_FROM_ESP(m_ble_tx_enqueue(buff, data, len, 0), OWNER_RIK_TX_SEND, return_on_full);
        }else if(_rik_wifi_active){
            //rep = wifi send function
        }else{
            return STA_C(ERR_INTERFACE_UNAVAILABLE, OWNER_RIK_TX_SEND, 0);
        }
    }else if(interface == 1){
    rep = STA_FROM_ESP(m_ble_tx_enqueue(buff, data, len, 0), OWNER_RIK_TX_SEND, return_on_full);
    }else if(interface == 2){
        //rep = wifi send function
    }else{
        return STA_C(ERR_INTERFACE_UNAVAILABLE, OWNER_RIK_TX_SEND, 0);
    }
    if(rep.e_code == ESP_ERR_NO_MEM){rep.e_code = ERR_TX_BUFFER_FULL;}
    return rep;
}

/**
 * @brief Send via default interface, non blocking
 * @param data_ptr uint8_t* buff
 * @param len size_t len to send
 * @return status_rep_t.e_code 0 on success, ERR_INTERFACE_UNAVAILABLE, ERR_TX_BUFFER_FULL, ESP_ERR_INVALID_ARG
 */
#define RIK_TX_NO_WAIT(data_ptr, len) rik_tx_send(rik_buff_tx, (const void*)(data_ptr), (len), 0, 1)
/**
 * @brief Send via default interface, blocking
 * @param data_ptr uint8_t* buff
 * @param len size_t len to send
 * @return status_rep_t.e_code 0 on success, ERR_INTERFACE_UNAVAILABLE, ERR_TX_BUFFER_FULL, ESP_ERR_INVALID_ARG
 */
#define RIK_TX_WAIT(data_ptr, len) rik_tx_send(rik_buff_tx, (const void*)(data_ptr), (len), 0, 0)

/**
 * @brief Send via ble interface, non blocking
 * @param data_ptr uint8_t* buff
 * @param len size_t len to send
 * @return status_rep_t.e_code 0 on success, ERR_INTERFACE_UNAVAILABLE, ERR_TX_BUFFER_FULL, ESP_ERR_INVALID_ARG
 */
#define RIK_TX_BLE_NO_WAIT(data_ptr, len) rik_tx_send(rik_buff_tx, (const void*)(data_ptr), (len), 1, 1)

/**
 * @brief Send via ble interface, blocking
 * @param data_ptr uint8_t* buff
 * @param len size_t len to send
 * @return status_rep_t.e_code 0 on success, ERR_INTERFACE_UNAVAILABLE, ERR_TX_BUFFER_FULL, ESP_ERR_INVALID_ARG
 */
#define RIK_TX_BLE_WAIT(data_ptr, len) rik_tx_send(rik_buff_tx, (const void*)(data_ptr), (len), 1, 0)

#define RIK_TX_LOG_NO_WAIT(data_ptr, len) rik_tx_send(rik_buff_esp_log, (const void*)(data_ptr), (len), 0, 1)

/**
 * @brief receive data from rx buffer
 * @param buff - space to store
 * @param buff_size - size of buff
 * @param len - len of received
 * @param wait_for_data - if true, will block until data is received, if false, will return immediately if no data
 * @return status_rep_t.e_code 0 on success, ERR_TIMEOUT if wait_for_data is true and no data received, ESP_ERR_INVALID_ARG if invalid args, ERR_SIZE_TOO_LARGE if received data is larger than buff_size
 */
static __inline status_rep_t rik_rx_receive(uint8_t* buff, size_t buff_size, size_t *len, bool wait_for_data){
    if (!rik_buff_rx || !buff || !len || !buff_size) { 
        return STA_C(ESP_ERR_INVALID_ARG, OWNER_RIK_RX_RECEIVE, 0);
    }

    void *item = xRingbufferReceive(rik_buff_rx, len, wait_for_data ? portMAX_DELAY : 0);

    if (item == NULL) {
        return STA_C(ERR_TIMEOUT, OWNER_RIK_RX_RECEIVE, 0);
    }

    if (*len > buff_size) {
        vRingbufferReturnItem(rik_buff_rx, item);
        return STA_C(ESP_ERR_INVALID_SIZE, OWNER_RIK_RX_RECEIVE, 0);
    }

    memcpy(buff, item, *len);
    vRingbufferReturnItem(rik_buff_rx, item);
    return STA_OK;
}

/**
 * @brief receive data from rx buffer, blocking
 * @param buff - space to store
 * @param len - len of received
 * @param buff_size - size of buff5
 * @return status_rep_t.e_code 0 on success, ERR_TIMEOUT if wait_for_data is true and no data received, ESP_ERR_INVALID_ARG if invalid args, ERR_SIZE_TOO_LARGE if received data is larger than buff_size
 */
#define RIK_RX_WAIT(buff, buff_size, len) rik_rx_receive((buff), (buff_size), (len), true)

/**
 * @brief receive data from rx buffer, non blocking
 * @param buff - space to store
 * @param len - len of received
 * @param buff_size - size of buff
 * @return status_rep_t.e_code 0 on success, ERR_TIMEOUT if wait_for_data is true and no data received, ESP_ERR_INVALID_ARG if invalid args, ERR_SIZE_TOO_LARGE if received data is larger than buff_size
 */
#define RIK_RX_NO_WAIT(buff, buff_size, len) rik_rx_receive((buff), (buff_size), (len), false)