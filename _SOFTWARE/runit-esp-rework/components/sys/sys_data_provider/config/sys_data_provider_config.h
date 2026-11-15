#pragma once

/**
 * @file sys_data_provider_config.h
 * @brief Provider state flags and provider types (UART, BLE, MQTT, Console...).
 */

/** @brief Provider driver types (u8 wire identifier in CREATE / INFO / LIST). */
//#ref-enum @alias Provider Type
typedef enum {
  SYS_DATA_PROVIDER_TYPE_UART    = 1,  //@alias UART @description UART bridge provider
  SYS_DATA_PROVIDER_TYPE_BLE     = 2,  //@alias Bluetooth LE @description BLE link provider
  SYS_DATA_PROVIDER_TYPE_MQTT    = 3,  //@alias MQTT @description MQTT topic provider
  SYS_DATA_PROVIDER_TYPE_CONSOLE = 4,  //@alias Console @description Board UART/USB-JTAG console
} sys_data_provider_type_e;

/* Provider state bits (filled by the driver inside create() and sys_data_provider_set_up(); OPEN is the registry's). */
//#ref-defines prov_state @alias Provider state @prefix SYS_DATA_PROVIDER_STATE_
#define SYS_DATA_PROVIDER_STATE_SYSTEM (1u << 0)  //@alias System @description The board's own: not removed
#define SYS_DATA_PROVIDER_STATE_OPEN   (1u << 1)  //@alias Open @description Its rx hook has an owner
#define SYS_DATA_PROVIDER_STATE_WIRED  (1u << 3)  //@alias Wired @description Connection realised by cable - low latency
#define SYS_DATA_PROVIDER_STATE_UP     (1u << 4)  //@alias Up @description It delivers frames now
#define SYS_DATA_PROVIDER_STATE_FRAMED (1u << 5)  //@alias Framed @description Its driver carries a whole CMD_FRAME_MAX frame; else raw
#define SYS_DATA_PROVIDER_STATE_NO_RX  (1u << 6)  //@alias No receive hook @description What comes in reaches no hook: owner reads from driver
