# Flashing the ESP32-S3 on the board's link through its ROM bootloader (SYS_FLASHER.MD).
idf_component_register(
    SRCS "sys_flasher.c" "rom/sys_flasher_rom.c" "router/sys_flasher_router.c"
    INCLUDE_DIRS "." "errors" "router"
    PRIV_INCLUDE_DIRS "rom"
    REQUIRES errors utils cmd router
    PRIV_REQUIRES sys_uart sys_session app_update esp_partition bootloader_support spi_flash esp_driver_gpio esp_rom
)
