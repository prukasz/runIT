/* The simulated SPI bus behind the esp_lcd_panel_io stand-in (esp_lcd_panel_io.h): one panel IO at a time, its bytes
 * delivered to the simulated controller (mock_ili9341.h), a colour transfer taking as long as its bytes need at the
 * IO's pclk_hz. The faults are what can go wrong on the wire (SYS_DISPLAY.MD "Transfer errors"). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MOCK_SPI_FAULT_NONE = 0,
    MOCK_SPI_FAULT_REFUSE,  /* tx_color fails at once (the bus refuses the transaction), nothing is sent */
    MOCK_SPI_FAULT_NOISE,   /* the colour bytes reach the panel with flipped bits; the callback comes: SPI has no ACK */
    MOCK_SPI_FAULT_HANG,    /* the next colour transfer never ends: no callback, until the IO is deleted; one-shot */
} mock_spi_fault_e;

/* For the next colour transfers (HANG: the next one only, then NONE); the IO keeps it across a delete. */
void mock_spi_set_fault(mock_spi_fault_e fault);
/* Prints every command with its parameters and every colour transfer's length as it goes on the wire. */
void mock_spi_set_trace(bool on);
