idf_component_register(
    SRCS "sys_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_errors
    PRIV_REQUIRES nvs_flash
)
