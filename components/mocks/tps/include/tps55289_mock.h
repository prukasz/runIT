#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/i2c_master.h"


/**
 * @brief Initialize the TPS55289 mock simulator task.
 */
void init_tps_mock(void);

/**
 * @brief Mock I2C transmit function for TPS55289.
 */
esp_err_t tps_transmit(i2c_master_dev_handle_t handle, const uint8_t *write_buffer, size_t write_buffer_len, int xfer_timeout_ms);

/**
 * @brief Mock I2C transmit_receive function for TPS55289.
 */
esp_err_t tps_transmit_receive(i2c_master_dev_handle_t handle, const uint8_t *write_buffer, size_t write_buffer_len, uint8_t *read_buffer, size_t read_buffer_len, int xfer_timeout_ms);

/**
 * @brief Get the simulated level of the FB/INT pin (0 = fault/active, 1 = normal/inactive).
 */
int tps_get_int_pin_level(void);


typedef void *(*tps_int_cb_t)(void);
void set_tca_int_callback(tps_int_cb_t cb);


//Opis
/*
    Rejestry:

    REF - Reference Voltage, adresy 00h 01h
    Wartość podzielona na dwa rejestry, na początku ustawić 00h później 01h,
    00000xxxb xxxxxxxxb - tam gdzie zero to reserved
        01h     00h
    
    np 5V = 00000001 10100100b

    IOUT_LIMIT - Current limit setting, adres 02h
    7 bit - EN, 6-0 - Wartość
    Deafult - 11100100b = 50mV (Różnica pomięczy napięcia pomiędzy pinem ISP a pinem ISN)

    VOUT_SR - Slew Rate , adres 03h
    7-6 Reserved
    5-4 OCP_Delay - Czas reakcji na overcurrent protection
    3-2 Reserved
    1-0 SR - Slew Rate

    VOUT_FS - Feedback selection adres 04h
    7 bit - Output feedback voltage selection 0 - internal, 1 - external
    6-2 Reserved
    1-0 Internal feedback ratio (Deafult 11b - 0.0564)

    CDC - Cable Compensation, adres 05h
    7 - SC_MASK - SC indication EN (SHORT CIRCUIT)
    6 - OCP_MASK - OCP indication EN (OVER CURRENT PROTECTION)
    5 - OVP_MASK - OVP indication EN (OVER VOLTAGE PROTECTION)
    4 - Reserved
    3 - CDC_OPTION - 0 internal cable compenstaion , 1 external by resistor
    2-0 - value for internal CDC

    MODE - Mode Control, adres 06h
    7 - OE - Output enable 
    6 - FSWDBL - swtching frequency 0 normal - 1 double in buck boost mode
    5 - HICCUP - En hiccup during SC protection
    4 - DISCHG - Output discharge
    3-2 Reserved
    1 - FPWM - operation mode at light load condition
    0 - Reserved


    STATUS - Operating Status adres 07h
    7 - SCP - short ciruit protection indication Does not reset until read
    6 - OCP - Over current protection indication Does not reset until read
    5 - OVP - Over voltage protection indication Does not reset until read
    4-2 Reserved
    1-0 Operating Status 


*/