idf_component_register(
    SRCS "status.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_ringbuf freertos esp_common manager_i2c runit_kernel ina3221_wrapper
)
