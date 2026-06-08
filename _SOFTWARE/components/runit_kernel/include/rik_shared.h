#pragma once
#include <freertos/ringbuf.h>
#include <driver/gpio.h>

extern uint8_t rik_current_monitor_id;
extern uint8_t rik_gpio_expander_id;
extern uint8_t rik_vreg0_id;
extern uint8_t rik_vreg1_id;
extern uint8_t rik_adc_expander_id;
extern uint8_t rik_pwm_expander_id;
extern uint8_t rik_power_delivery_id;

extern uint8_t rik_gpio_expander_port_id; 
extern uint8_t rik_adc_expander_port_id;
extern uint8_t rik_gpio_esp_port_id;
extern uint8_t rik_pwm_expander_port_id;


#define RIK_IO_PIN_PWM_EXPANDER_nOE  SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 0)

#define RIK_IO_PIN_REGA_INT   SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 1)
#define RIK_IO_PIN_REGA_EN   SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 17) //21

#define RIK_IO_PIN_REGB_INT   SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 2) //2
#define RIK_IO_PIN_REGB_EN   SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 16) //20

#define RIK_IO_PIN_VUSB_OK    SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 3)  //3
#define RIK_IO_PIN_VEXT_OK    SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 4)  //4

#define RIK_IO_PIN_CURRENT_MONITOR_WARN    SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 5) //5
#define RIK_IO_PIN_CURRENT_MONITOR_CRIT   SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 6) //6

#define RIK_IO_PIN_DRV_OCPM  SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 7) //7

#define RIK_IO_PIN_DRV2_SLEEP   SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 8)  //10
#define RIK_IO_PIN_DRV2_FAULT   SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 10)  //12
#define RIK_IO_PIN_DRV2_VMA_REGA SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 12)  //14
#define RIK_IO_PIN_DRV2_VMB_VSUP SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 13)  //15

#define RIK_IO_PIN_DRV1_SLEEP   SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 9)  //11
#define RIK_IO_PIN_DRV1_FAULT   SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 11)  //13
#define RIK_IO_PIN_DRV1_VMA_VSUP SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 14) //16
#define RIK_IO_PIN_DRV1_VMB_REGB SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 15) //17

#define RIK_IO_PIN_PD_INT  SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 21)   //25

#define RIK_IO_PIN_LED_1  SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 23)   //27
#define RIK_IO_PIN_LED_2   SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 22)   //26

/**********ADC EXPANDER ******************************/
#define RIK_IO_PIN_ADC_0  SYS_IO_MAKE_PIN(rik_adc_expander_port_id, 0)
#define RIK_IO_PIN_ADC_1  SYS_IO_MAKE_PIN(rik_adc_expander_port_id, 1)
#define RIK_IO_PIN_ADC_2  SYS_IO_MAKE_PIN(rik_adc_expander_port_id, 2)
#define RIK_IO_PIN_ADC_3  SYS_IO_MAKE_PIN(rik_adc_expander_port_id, 3)
#define RIK_IO_PIN_ADC_4  SYS_IO_MAKE_PIN(rik_adc_expander_port_id, 4)
#define RIK_IO_PIN_ADC_5  SYS_IO_MAKE_PIN(rik_adc_expander_port_id, 5)
#define RIK_IO_PIN_ADC_6  SYS_IO_MAKE_PIN(rik_adc_expander_port_id, 6)
#define RIK_IO_PIN_ADC_7  SYS_IO_MAKE_PIN(rik_adc_expander_port_id, 7)

#define RIK_IO_PIN_GPIO_EXPANDER_nINT       SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 9)
#define RIK_IO_PIN_GPIO_EXPANDER_nRESET     SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 8)
#define RIK_IO_PIN_ADC_EXPANDER_ALERT       SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 42)

// ---------------------------------------------------------
// Motor Driver 1 - Current Sense (IPROPI)
// ---------------------------------------------------------
#define RIK_IO_PIN_DRV_1_IPROPI_1     SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 7)
#define RIK_IO_PIN_DRV_1_IPROPI_2     SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 6)
#define RIK_IO_PIN_DRV_1_IPROPI_3     SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 5)
#define RIK_IO_PIN_DRV_1_IPROPI_4     SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 4)

// ---------------------------------------------------------
// Motor Driver 1 - Inputs (IN)
// ---------------------------------------------------------
#define RIK_IO_PIN_DRV_1_IN1          SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 21)
#define RIK_IO_PIN_DRV_1_IN2          SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 47)
#define RIK_IO_PIN_DRV_1_IN3          SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 48)
#define RIK_IO_PIN_DRV_1_IN4          SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 45)

// ---------------------------------------------------------
// Motor Driver 1 - Enables (EN)
// ---------------------------------------------------------
#define RIK_IO_PIN_DRV_1_EN1          SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 38)
#define RIK_IO_PIN_DRV_1_EN2          SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 39)
#define RIK_IO_PIN_DRV_1_EN3          SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 2)
#define RIK_IO_PIN_DRV_1_EN4          SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 1)


// ---------------------------------------------------------
// PWM Expander Channels (PCA9685)
// ---------------------------------------------------------
#define RIK_PWM_EXPANDER_USER_CHANNEL_0  SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 0)
#define RIK_PWM_EXPANDER_USER_CHANNEL_1  SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 1)
#define RIK_PWM_EXPANDER_USER_CHANNEL_2  SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 2)
#define RIK_PWM_EXPANDER_USER_CHANNEL_3  SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 3)
#define RIK_PWM_EXPANDER_USER_CHANNEL_4  SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 4)
#define RIK_PWM_EXPANDER_USER_CHANNEL_5  SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 5)
#define RIK_PWM_EXPANDER_USER_CHANNEL_6  SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 6)
#define RIK_PWM_EXPANDER_USER_CHANNEL_7  SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 7)
#define RIK_IO_PIN_DRV_2_IN1  SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 8)
#define RIK_IO_PIN_DRV_2_IN2  SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 9)
#define RIK_IO_PIN_DRV_2_IN3 SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 10)
#define RIK_IO_PIN_DRV_2_IN4 SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 11)
#define RIK_IO_PIN_DRV_2_EN1 SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 12)
#define RIK_IO_PIN_DRV_2_EN2 SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 13)
#define RIK_IO_PIN_DRV_2_EN3 SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 14)
#define RIK_IO_PIN_DRV_2_EN4 SYS_IO_MAKE_PIN(rik_pwm_expander_port_id, 15)


// ---------------------------------------------------------
// External SPI Bus
// ---------------------------------------------------------
#define RIK_IO_PIN_USR_SPI_CS0_10     SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 10)
#define RIK_IO_PIN_USR_SPI_MOSI_11    SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 11)
#define RIK_IO_PIN_USR_SPI_SCK_12     SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 12)
#define RIK_IO_PIN_USR_SPI_MISO_13    SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 13)
#define RIK_IO_PIN_USR_SPI_CS1_14     SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 14)

// ---------------------------------------------------------
// I2C Buses
// ---------------------------------------------------------
// Internal I2C (Connects to internal sensors/expanders)
#define RIK_IO_PIN_INTERNAL_I2C_SDA        SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 15)
#define RIK_IO_PIN_INTERNAL_I2C_SCL        SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 16)

// External I2C (Connects to the J_EXT_I2C header)
#define RIK_IO_PIN_USR_I2C_SDA        SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 40)
#define RIK_IO_PIN_USR_I2C_SCL        SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 41)


// ---------------------------------------------------------
// UART0 (Console / External UART)
// ---------------------------------------------------------
#define RIK_IO_PIN_EXT_UART_TX        SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 43)
#define RIK_IO_PIN_EXT_UART_RX        SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 44)


// ---------------------------------------------------------
// Miscellaneous External User Pins
// ---------------------------------------------------------
#define RIK_IO_PIN_USR_3              SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 3)   // Connected to J_MISC1 Pin 1
#define RIK_IO_PIN_USR_17             SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 17)  // Connected to J_MISC1 Pin 2
#define RIK_IO_PIN_USR_18             SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 18)  // Connected to J_MISC1 Pin 3
#define RIK_IO_PIN_USR_46             SYS_IO_MAKE_PIN(rik_gpio_esp_port_id, 46)  // Connected to J_MISC1 Pin 4

#define RIK_CHANNEL_VREG0 0 
#define RIK_CHANNEL_TOTAL 1
#define RIK_CHANNEL_VREG1 2



extern bool _rik_ble_active;
extern bool _rik_wifi_active;
extern bool _rik_emergency_state_active;

extern RingbufHandle_t rik_buff_status;
extern RingbufHandle_t rik_buff_tx;
extern RingbufHandle_t rik_buff_rx;
extern RingbufHandle_t rik_buff_esp_log;

extern EventGroupHandle_t rik_events_wireless;
extern EventGroupHandle_t rik_events_data_processing;
extern EventGroupHandle_t rik_events_wired;
extern EventGroupHandle_t rik_events_vm;

/*Event group of data interfaces */
/**************************BLE*********************************************/
#define EVENT_BIT_BLE_TX_START          (1 << 0)
#define EVENT_BIT_BLE_TX_DONE           (1 << 1)
#define EVENT_BIT_BLE_ON_RX             (1 << 2)
#define EVENT_BIT_BLE_ON_RX_FAILED      (1 << 3)

#define EVENT_BIT_BLE_CONNECTED         (1 << 4)
#define EVENT_BIT_BLE_CONNECTION_FAILED (1 << 5)
#define EVENT_BIT_BLE_DISCONNECTED      (1 << 6)
#define EVENT_BIT_BLE_MTU_UPDATED       (1 << 7)
/**************************BLE*********************************************/


/**************************I2C *********************************/
#define EVENT_BIT_I2C_PROCESS_0   (1 << 0)
#define EVENT_BIT_I2C_PROCESS_1   (1 << 1)
#define EVENT_BIT_I2C_DONE_0      (1 << 2)
#define EVENT_BIT_I2C_DONE_1      (1 << 3)
/**************************I2C *********************************/

/*Event group of data interfaces */

/**************************VM INTERFACE  *********************************/
#define EVENT_BIT_VM_WIRELESS_CONNECTION_PRESENT (1 << 0) // rik -> vm
#define EVENT_BIT_VM_RUN            (1 << 13)
#define EVENT_BIT_VM_READY          (1 << 1) // vm -> rik
#define EVENT_BIT_VM_OFFLINE_MODE   (1 << 2) // vm -> rik  invoked by remote
#define EVENT_BIT_VM_ONLINE_MODE    (1 << 3) // vm -> rik  invoked by remote
#define EVENT_BIT_VM_CMD_COMPLETE   (1 << 4) // vm -> rik
#define EVENT_BIT_VM_STOP           (1 << 9) // rik -> vm
#define EVENT_BIT_VM_EMERGENCY      (1 << 11) // rik -> vm
#define EVENT_BIT_VM_RESET          (1 << 12) // rik -> vm

/*************************VM  INTERFACE  *********************************/


/************************STATUS HANDLER ACTIONS **************************/
#define EVENT_BIT_I2C_FAILURE               (1<<1)
#define EVENT_BIT_GPIO_FAILURE              (1<<2)
#define EVENT_BIT_ADC_FAILURE               (1<<3)
#define EVENT_BIT_PWM_FAILURE               (1<<4)
#define EVENT_BIT_POWER_FAILURE             (1<<5)
#define EVENT_BIT_VM_FAILURE_USER           (1<<6)
#define EVENT_BIT_VM_FAILURE_ENGINE         (1<<7)
#define EVENT_BIT_DATA_PROCESSING_FAILURE   (1<<8)
/************************STATUS HANDLER ACTIONS **************************/
