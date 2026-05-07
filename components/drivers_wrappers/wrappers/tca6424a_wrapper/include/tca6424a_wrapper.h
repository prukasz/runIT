#pragma once 
#include <stdint.h>
#include <esp_err.h>
#include "tca6424a.h"


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

/**
 * @brief Set one of onboard LEDs
 * @param led_num LED number (0 or 1)
 * @param state true to turn on, false to turn off
 */
esp_err_t io_sys_led_set(bool led_num, bool state);

/**
 * @brief Enable or disable PWM output of PCA9685
 * @param state true to enable, false to disable
 */
esp_err_t io_sys_pwm_en(bool state);

/**
 * @brief Enable or disable voltage regulator output (TPS55289)
 * @param vreg_num Voltage regulator number (0 or 1)
 * @param state true to enable, false to disable
 */
esp_err_t io_sys_vreg_en(bool vreg_num, bool state);

/**
 * @brief Select mode for overcurrent protection on DRV outputs
 * @param mode mode "1" or "0"
 */
esp_err_t io_sys_drv_ocpm(bool mode);

/**
 * @brief Enable or disable sleep mode for DRV
 * @param drv_num Driver number (0 or 1)
 * @param state true to enable, false to disable
 */
esp_err_t io_sys_drv_sleep(bool drv_num, bool state);

/**
 * @brief Set active LM73100
 * @param drv_num Driver number (0 or 1)
 * @param sup_num Supply number (0 or 1)
 */
esp_err_t io_sys_drv_set_suplly(bool drv_num, bool sup_num);

/**
 * @brief Turn off both LM73100
 * @param drv_num Driver number (0 or 1)
 */
esp_err_t io_sys_drv_reset_supply(bool drv_num);

/**
 * @brief Register callback for DRV fault pin
 * @param drv_num Driver number (0 or 1)
 */
esp_err_t io_sys_callback_drv_set(bool drv_num, void (*drv_callback)(void* arg), void* arg);

/**
 * @brief Register callback for AP33772s PD interrupt pin
 * @param usb_callback Function pointer to the callback function
 * @param arg Argument to be passed to the callback function
 */
esp_err_t io_sys_callback_usb_set(void (*usb_callback)(void* arg), void* arg);

/**
 * @brief Register callback for INA3221 warning pin 
 * @param ina_callbac_x Function pointer to the callback function
 * @param arg_x Argument to be passed to the callback function (e.g. ina3221 handle)
 * @warning Callback provided by ina3221 driver
 */
esp_err_t io_sys_callback_ina3221_set(void (*ina_callback_c)(void* arg), void* arg_c, void (*ina_callback_w)(void* arg), void* arg_w);


esp_err_t tca_wrapper_init(void* dev);
