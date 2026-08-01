idf_component_register(
    SRCS "sys_actions.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_errors
    PRIV_REQUIRES utils sys_interface codecs nvs_flash sys_device
)
