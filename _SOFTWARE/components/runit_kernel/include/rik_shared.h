#pragma once
#include <freertos/ringbuf.h>
#include <driver/gpio.h>

extern uint8_t rik_ina_id;
extern uint8_t rik_gpio_expander_id;
extern uint8_t rik_tps_0_id;
extern uint8_t rik_tps_1_id;

extern uint8_t rik_gpio_expander_port_id; 

#define SYS_IO_MAKE_PIN(port, pin) ((((uint32_t)(port)) << 8) | ((pin) & 0xFF))

#define RIK_IO_PIN_PWM_EXPANDER_nOE  SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 0)

#define RIK_IO_PIN_REGA_INT   SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 1)
#define RIK_IO_PIN_REGA_EN   SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 17) //21

#define RIK_IO_PIN_REGB_INT   SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 2) //2
#define RIK_IO_PIN_REGB_EN   SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 16) //20

#define RIK_IO_PIN_VUSB_OK    SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 3)  //3
#define RIK_IO_PIN_VEXT_OK    SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 4)  //4

#define RIK_IO_PIN_POWER_MONITOR_WARN    SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 5) //5
#define RIK_IO_PIN_MONITOR_CRIT   SYS_IO_MAKE_PIN(rik_gpio_expander_port_id, 6) //6

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


extern bool _rik_ble_active;
extern bool _rik_wifi_active;
extern bool _rik_emergency_state_active;

extern RingbufHandle_t rik_buff_status;
extern RingbufHandle_t rik_buff_tx;
extern RingbufHandle_t rik_buff_rx;
extern RingbufHandle_t rik_buff_log;

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
#define EVENT_BIT_VM_READY          (1 << 1) // vm -> rik
#define EVENT_BIT_VM_OFFLINE_MODE   (1 << 2) // vm -> rik  invoked by remote
#define EVENT_BIT_VM_ONLINE_MODE    (1 << 3) // vm -> rik  invoked by remote
#define EVENT_BIT_VM_CMD_COMPLETE   (1 << 4) // vm -> rik
#define EVENT_BIT_VM_STOP           (1 << 9) // rik -> vm
#define EVENT_BIT_VM_EMERGENCY      (1 << 11) // rik -> vm
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


/*Event group of data processing / vm */



#define GPIO_NUM_40 40
#define GPIO_NUM_41 41
#define GPIO_NUM_42 42   
#define GPIO_NUM_43 43
#define GPIO_NUM_44 44
#define GPIO_NUM_45 45
#define GPIO_NUM_46 46
#define GPIO_NUM_47 47
#define GPIO_NUM_48 48

// ---------------------------------------------------------
// Interrupts and Alerts
// ---------------------------------------------------------
#define IO_SYS_PIN_TCA6424_nINT    GPIO_NUM_9
#define IO_SYS_PIN_TCA6424_nRESET  GPIO_NUM_8
#define IO_SYS_PIN_ADS_ALERT       GPIO_NUM_42

// ---------------------------------------------------------
// Motor Driver 1 - Current Sense (IPROPI)
// ---------------------------------------------------------
#define IO_SYS_PIN_DRV_1_IPROPI_1  GPIO_NUM_7
#define IO_SYS_PIN_DRV_1_IPROPI_2  GPIO_NUM_6
#define IO_SYS_PIN_DRV_1_IPROPI_3  GPIO_NUM_5
#define IO_SYS_PIN_DRV_1_IPROPI_4  GPIO_NUM_4

// ---------------------------------------------------------
// Motor Driver 1 - Inputs (IN)
// ---------------------------------------------------------
#define IO_SYS_PIN_DRV_1_IN1       GPIO_NUM_21
#define IO_SYS_PIN_DRV_1_IN2       GPIO_NUM_47
#define IO_SYS_PIN_DRV_1_IN3       GPIO_NUM_48
#define IO_SYS_PIN_DRV_1_IN4       GPIO_NUM_45

// ---------------------------------------------------------
// Motor Driver 1 - Enables (EN)
// ---------------------------------------------------------
#define IO_SYS_PIN_DRV_1_EN1       GPIO_NUM_38
#define IO_SYS_PIN_DRV_1_EN2       GPIO_NUM_39
#define IO_SYS_PIN_DRV_1_EN3       GPIO_NUM_2
#define IO_SYS_PIN_DRV_1_EN4       GPIO_NUM_1

// ---------------------------------------------------------
// External SPI Bus
// ---------------------------------------------------------
#define IO_SYS_PIN_USR_SPI_CS0_10   GPIO_NUM_10
#define IO_SYS_PIN_USR_SPI_MOSI_11  GPIO_NUM_11
#define IO_SYS_PIN_USR_SPI_SCK_12   GPIO_NUM_12
#define IO_SYS_PIN_USR_SPI_MISO_13  GPIO_NUM_13
#define IO_SYS_PIN_USR_SPI_CS1_14   GPIO_NUM_14

// ---------------------------------------------------------
// I2C Buses
// ---------------------------------------------------------
// Internal I2C (Connects to internal sensors/expanders)
#define IO_SYS_PIN_INT_I2C_SDA   GPIO_NUM_15
#define IO_SYS_PIN_INT_I2C_SCL   GPIO_NUM_16

// External I2C (Connects to the J_EXT_I2C header)
#define IO_SYS_PIN_USR_I2C_SDA   GPIO_NUM_40
#define IO_SYS_PIN_USR_I2C_SCL   GPIO_NUM_41


// ---------------------------------------------------------
// UART0 (Console / External UART)
// ---------------------------------------------------------
#define IO_SYS_PIN_EXT_UART_TX   GPIO_NUM_43 
#define IO_SYS_PIN_EXT_UART_RX   GPIO_NUM_44


#define IO_SYS_PIN_USR_3   GPIO_NUM_3   // Connected to J_MISC1 Pin 1
#define IO_SYS_PIN_USR_17   GPIO_NUM_17  // Connected to J_MISC1 Pin 2
#define IO_SYS_PIN_USR_18  GPIO_NUM_18  // Connected to J_MISC1 Pin 3
#define IO_SYS_PIN_USR_46  GPIO_NUM_46  // Connected to J_MISC1 Pin 4
