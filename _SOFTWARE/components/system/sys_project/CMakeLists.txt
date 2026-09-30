idf_component_register(
    SRCS "sys_project.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_errors
    PRIV_REQUIRES utils sys_interface sys_settings nvs_flash esp_rom esp_system freertos log
)
