idf_component_register(
    SRCS "sys_power.c" "sys_power_manager.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES utils sys_device sys_settings
    REQUIRES sys_errors sys_event sys_io
)
