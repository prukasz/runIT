idf_component_register(
    SRCS "sys_actions.c" "sys_actions_static.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_errors
    PRIV_REQUIRES utils sys_interface nvs_flash sys_device VM
)
