idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES errors time utils router router_queue timer sys_event sys_device sys_device_event sys_device_timer sys_device_router sys_device_policy sys_io sys_io_claim sys_power sys_hbridge sys_power_manager sys_i2c device_esp32s3_gpio device_esp32s3_i2c device_dc_motor device_pca9685 device_tca6424a
)
