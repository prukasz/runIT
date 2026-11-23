# The SPI contract (a bus is a device) and its client (a chip on a bus): sys_spi.h. No bus is made here: the ESP32's
# controller is the device device_esp32_spi. ".." for the module-prefixed includes ("sys_spi/sys_spi_id.h").
# WHOLE_ARCHIVE: registers the errors of MODULE_ID_SPI even when nothing calls a client function directly.
idf_component_register(
    SRCS "sys_spi.c"
    INCLUDE_DIRS "." "errors" ".."
    REQUIRES errors cmd sys_device
    WHOLE_ARCHIVE
)
