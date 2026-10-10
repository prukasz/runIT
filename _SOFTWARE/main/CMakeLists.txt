idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES utils log errors cmd router sys_i2c sys_uart sys_ble sys_connector sys_device sys_actions sys_event sys_io
)
