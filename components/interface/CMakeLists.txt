idf_component_register(
    SRCS "interface_dispatcher.c"
    INCLUDE_DIRS "include"
    REQUIRES "freertos" "esp_ringbuf" "pca9685"
)
