#pragma once 
#include "status.h"



/*!!!!!!! remove !!!!!!!!*/
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
/*!!!!!!! remove !!!!!!!!*/



void * provider_gpio_expander_new_handle(uint8_t i2c_addr);
void _sys_expander_gpio_delay_updates(bool dereffered_mode);

status_rep_t _sys_io_expander_set_pin(uint64_t pin_mask, bool state);
status_rep_t _sys_expander_gpio_set_callback(uint64_t pin_mask, uint32_t mode, void (*callback)(void* arg), void* arg);
status_rep_t _sys_expander_configure_pins(uint64_t pin_mask, uint32_t mode_mask);
status_rep_t _sys_io_expander_read_pins(uint64_t* out_mask);
status_rep_t _sys_io_expander_read_pin(uint64_t pin_mask, bool* out_mask);
status_rep_t _sys_io_expander_toggle_pin(uint64_t pin_mask);

i2c_master_dev_handle_t provider_gpio_expander_get_i2c_dev_handle();
TaskHandle_t provider_gpio_expander_get_task_handle();
i2c_device_config_t* provider_gpio_expander_get_i2c_dev_config();


