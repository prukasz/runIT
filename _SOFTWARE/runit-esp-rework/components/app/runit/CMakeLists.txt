# The firmware's composition (RUNIT.MD): the boot order, the board's own devices, providers and streams, the error sink.
# The devices are linked WHOLE_ARCHIVE by their own CMake: listing them here is what puts them in the image.
idf_component_register(
    SRCS "runit.c" "connectivity/runit_streams.c" "connectivity/runit_ways.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "connectivity"
    PRIV_REQUIRES errors time utils cmd router router_queue timer sys_nvs sys_actions sys_board sys_data_provider
                  provider_console provider_uart sys_ble provider_ble sys_session sys_system sys_event
                  data_matrix sys_log sys_watch sys_remote sys_device sys_device_event sys_device_timer sys_device_router
                  sys_device_policy sys_io sys_power sys_hbridge sys_power_manager sys_i2c device_esp32s3_gpio
                  device_esp32s3_i2c device_esp32s3_uart device_dc_motor device_pca9685 device_tca6424a device_tps55289
                  log
)
