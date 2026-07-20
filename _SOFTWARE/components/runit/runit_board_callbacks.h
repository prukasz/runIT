#pragma once
#include "sys_ble.h"

void sys_on_ble_connect(uint16_t conn_id) {
}

void sys_on_ble_disconnect(uint16_t conn_handle, int reason) {
}

void sys_on_ble_failure(esp_err_t code) {
}