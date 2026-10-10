idf_component_register(
    SRCS "sys_i2c.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES utils
    REQUIRES sys_errors esp_driver_i2c 
)
