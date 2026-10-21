idf_component_register(
    SRCS "router/sys_i2c_router.c" "sys_i2c.c"
    INCLUDE_DIRS "." "errors" "router"
    REQUIRES errors esp_driver_i2c
    PRIV_REQUIRES cmd router utils
)
