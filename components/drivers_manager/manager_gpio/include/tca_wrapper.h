#pragma once 
#include "status.h"


#define IO_TCA_PWM_nOE    1LU //0

#define IO_TCA_REGA_INT   (1LU << 1) //1
#define IO_TCA_REGA_EN   (1LU << 17) //21

#define IO_TCA_REGB_INT   (1LU << 2) //2
#define IO_TCA_REGB_EN   (1LU << 16) //20

#define IO_TCA_VUSB_OK    (1LU << 3)  //3
#define IO_TCA_VEXT_OK    (1LU << 4)  //4

#define IO_TCA_INA3221_WARN   (1LU << 5) //5
#define IO_TCA_INA3221_CRIT   (1LU << 6) //6

#define IO_TCA_DRV_OCPM (1LU << 7)   //7

#define IO_TCA_DRV2_SLEEP (1LU << 8)  //10
#define IO_TCA_DRV2_FAULT (1LU << 10)  //12
#define IO_TCA_DRV2_VMA_REGA (1LU << 12)  //14
#define IO_TCA_DRV2_VMB_VSUP (1LU << 13)  //15


#define IO_TCA_DRV1_SLEEP (1LU << 9)  //11
#define IO_TCA_DRV1_FAULT (1LU << 11)  //13
#define IO_TCA_DRV1_VMA_VSUP (1LU << 14) //16
#define IO_TCA_DRV1_VMB_REGB (1LU << 15) //17

#define IO_TCA_PD_INT  (1LU << 21)   //25

#define IO_TCA_LED_1   (1LU << 23)   //27
#define IO_TCA_LED_2   (1LU << 22)   //26

esp_err_t io_sys_led_set(bool led_num, bool state);

esp_err_t io_sys_pwm_en(bool state);

esp_err_t io_sys_vreg_en(bool vreg_num, bool state);

esp_err_t io_sys_drv_ocpm(bool mode);

esp_err_t io_sys_drv_sleep(bool drv_num, bool state);

esp_err_t io_sys_drv_set_suplly(bool drv_num, bool sup_num);

esp_err_t io_sys_drv_reset_supply(bool drv_num);

esp_err_t io_sys_drv_callback_set(bool drv_num, void (*drv_callback)(void* arg), void* arg);

esp_err_t io_sys_usb_callback_set(void (*usb_callback)(void* arg), void* arg);

esp_err_t io_sys_ina3221_callback_set(void (*ina_callback)(void* arg), void* arg);

esp_err_t io_sys_callback_on_supply_type(void (*vsup_callback)(void* arg), void* arg);

void tca_wrapper_init(void* dev);

void tca_set_pins(void* dev, uint32_t pins_mask, uint32_t pins_state);
uint32_t tca_get_pins(void* dev);
void tca_cfg_pins(uint32_t pins_mode, uint32_t pins_polarity);















