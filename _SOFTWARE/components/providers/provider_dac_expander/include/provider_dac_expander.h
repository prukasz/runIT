#pragma once
#include "status.h"
#include "driver/i2c_master.h"
#include "dac53202.h"

void* p_dac_expander_new(uint8_t i2c_addr);

i2c_device_config_t* p_dac_expander_get_i2c_dev_config(void);
i2c_master_dev_handle_t* p_dac_expander_get_i2c_dev_handle(void);
TaskHandle_t p_dac_expander_get_task_handle(void);

void p_dac_expander_freeze(bool freeze);

status_rep_t p_dac_expander_configure(void);
status_rep_t p_dac_expander_set_channel_mv(uint8_t channel, uint32_t mv);
status_rep_t p_dac_expander_get_channel_mv(uint8_t channel, uint32_t *mv);
status_rep_t p_dac_expander_reset(void);
