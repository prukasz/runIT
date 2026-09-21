#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "device_drv8962.h"
#include "sys_hbridge.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct drv8962_dev_t* drv8962_handle_t;

drv8962_handle_t drv8962_new(const d_drv8962_cfg_t* cfg);
err_h drv8962_start(drv8962_handle_t handle);
err_h drv8962_stop(drv8962_handle_t handle);

err_h drv8962_set_mode(drv8962_handle_t handle, uint8_t channel, sys_hbridge_mode_e mode);
err_h drv8962_set_drive(drv8962_handle_t handle, uint8_t channel, float magnitude);
err_h drv8962_brake(drv8962_handle_t handle, uint8_t channel);
err_h drv8962_coast(drv8962_handle_t handle, uint8_t channel);

err_h drv8962_get_current_ma(drv8962_handle_t handle, uint8_t channel, uint32_t* out_ma);
err_h drv8962_set_current_limit_ma(drv8962_handle_t handle, uint8_t channel, uint32_t limit_ma);

err_h drv8962_configure_fault(drv8962_handle_t handle, uint8_t channel, const sys_hbridge_fault_config_t* config);
err_h drv8962_get_fault(drv8962_handle_t handle, uint8_t channel, bool* out_fault, sys_hbridge_fault_reason_e* out_reason);
err_h drv8962_clear_fault(drv8962_handle_t handle, uint8_t channel);

#ifdef __cplusplus
}
#endif

