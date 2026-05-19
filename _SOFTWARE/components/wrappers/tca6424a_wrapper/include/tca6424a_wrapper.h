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

#define MODE_RISING_EDGE 1
#define MODE_FALLING_EDGE 2
#define MODE_ANY_EDGE 3

extern void tca_isr_callback(void* arg);

/**
 * @brief Set one of onboard LEDs
 * @param led_num LED number (0 or 1)
 * @param state true to turn on, false to turn off
 */
status_rep_t sys_io_set_led(bool led_num, bool state);


/**
 * @brief Set the state of one or more expander pins defined in the outputs_mask (e.g. IO_TCA_REGA_EN, IO_TCA_DRV1_SLEEP, etc.)
 * @param pin_mask Bitmask of the pin(s) to set (use IO_TCA
 * @note only pins as output allowed
 */
status_rep_t sys_io_expander_set_pin(uint32_t pin_mask, bool state);
/**
 * @brief Register callback for expander pin interrupts (on pin change)
 * @param pin_mask Bitmask of the pin(s) to register the callback for (use IO_TCA_ defines)
 * @param mode Interrupt mode: MODE_RISING_EDGE, MODE_FALLING_EDGE, or MODE_ANY_EDGE (1,2,3)
 */
void sys_io_expander_set_pin_callback(uint32_t pin_mask, uint32_t mode, void (*callback)(void* arg), void* arg);

// status_rep_t io_sys_periph_reset_drv_supply(bool drv_num);

// /**
//  * @brief Register callback for DRV fault pin
//  * @param drv_num Driver number (0 or 1)
//  */
// status_rep_t io_sys_callback_set_drv(bool drv_num, void (*drv_callback)(void* arg), void* arg);

// /**
//  * @brief Register callback for AP33772s PD interrupt pin
//  * @param usb_callback Function pointer to the callback function
//  * @param arg Argument to be passed to the callback function
//  */
// status_rep_t io_sys_callback_set_usb(void (*usb_callback)(void* arg), void* arg);

// /**
//  * @brief Register callback for INA3221 warning pin 
//  * @param ina_callbac_x Function pointer to the callback function
//  * @param arg_x Argument to be passed to the callback function (e.g. ina3221 handle)
//  * @warning Callback provided by ina3221 driver
//  */
// status_rep_t io_sys_callback_set_ina3221(void (*ina_callback_c)(void* arg), void* arg_c,
//  void (*ina_callback_w)(void* arg), void* arg_w);

// status_rep_t sys_io_init_expander(void* dev);

// status_rep_t io_sys_callback_set_tps55289(bool vreg_num, void (*tps_callback)(void* arg), void* arg);
