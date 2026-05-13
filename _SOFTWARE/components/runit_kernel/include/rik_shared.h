#pragma once
#include <freertos/ringbuf.h>
#include <driver/gpio.h>

extern uint8_t rik_ina_id;
extern uint8_t rik_tca_id;
extern uint8_t rik_tps_0_id;
extern uint8_t rik_tps_1_id;

extern bool _rik_ble_active;
extern bool _rik_wifi_active;

extern RingbufHandle_t rik_buff_status;
extern RingbufHandle_t rik_buff_tx;
extern RingbufHandle_t rik_buff_rx;
extern RingbufHandle_t rik_buff_log;

extern EventGroupHandle_t rik_events_communication;
extern EventGroupHandle_t rik_events_processing;
extern EventGroupHandle_t rik_i2c_events_0;
extern EventGroupHandle_t rik_i2c_events_1;

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
#define EVENT_BIT_I2C_PROCESS   (1 << 8)
#define EVENT_BIT_I2C_DONE      (1 << 9)
#define EVENT_BIT_I2C_EMERGENCY (1 << 10)
#define EVENT_BIT_I2C_TIMEOUT   (1 << 11)
/**************************I2C *********************************/

/*Event group of data interfaces */


/*Event group of data processing / vm */
/**************************CMD INTERFACE *********************************/
#define EVENT_BIT_INTERFACE_CMD_START      (1 << 0)
#define EVENT_BIT_INTERFACE_CMD_COMPLETE   (1 << 1)
#define EVENT_BIT_INTERFACE_CMD_ERROR      (1 << 2)
#define EVENT_BIT_INTERFACE_CMD_STOP       (1 << 3)
/**************************CMD INTERFACE *********************************/

/**************************VM INTERFACE  *********************************/
#define EVENT_BIT_VM_RUN        (1 << 8)
#define EVENT_BIT_VM_STOP       (1 << 9)
#define EVENT_BIT_VM_ERROR      (1 << 10)
#define EVENT_BIT_VM_EMERGENCY  (1 << 11)
/*************************VM  INTERFACE  *********************************/

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
