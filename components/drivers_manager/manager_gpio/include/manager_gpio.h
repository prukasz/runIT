#pragma once 
#include "status.h"


#define IO_TCA_PWM_nOE    0 

#define IO_TCA_REGA_INT   1
#define IO_TCA_REGA_EN   21

#define IO_TCA_REGB_INT   2
#define IO_TCA_REGA_EN   20

#define IO_TCA_VUSB_OK    3
#define IO_TCA_VEXT_OK    4

#define IO_TCA_INA3221_WARN   5
#define IO_TCA_INA3221_CRIT   6

#define IO_TCA_DRV_OCPM 7

#define IO_TCA_DRV2_SLEEP 10
#define IO_TCA_DRV2_FAULT 12
#define IO_TCA_DRV2_VMA_REGA 14
#define IO_TCA_DRV2_VMB_VSUP 15


#define IO_TCA_DRV1_SLEEP 11
#define IO_TCA_DRV1_FAULT 13
#define IO_TCA_DRV1_VMA_VSUP 16 
#define IO_TCA_DRV1_VMB_REGB 17

#define IO_TCA_PD_INT 25

#define IO_TCA_LED_1   27
#define IO_TCA_LED_2   26

status_err_report_t io_sys_led_1_set(bool led_num, bool state);

status_err_report_t io_sys_pwm_en(bool state);

status_err_report_t io_sys_vreg_en(bool vreg_num, bool state);

status_err_report_t io_sys_drv_ocpm(bool mode);

status_err_report_t io_sys_drv_sleep(bool drv_num, bool state);

status_err_report_t io_sys_drv_set_suplly(bool drv_num, bool sup_num);

status_err_report_t io_sys_drv_callback_set(void (*drv_callback)(uint32_t pins));

status_err_report_t io_sys_usb_callback_set(void (*usb_callback)(uint32_t pins));

status_err_report_t io_sys_ina3221_callback_set(void (*ina_callback)(uint32_t pins));

status_err_report_t io_sys_callback_on_supply_type(void (*ina_callback)(uint32_t pins));

void tca_set_pins(void* dev, uint32_t mask);
uint32_t tca_get_pins(void* dev);
void tca_cfg_pins(uint32_t pins_mode, uint32_t pins_polarity);















