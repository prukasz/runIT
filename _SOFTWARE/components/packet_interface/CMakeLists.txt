idf_component_register(
    SRCS "interface_dispatcher.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_ringbuf runit_kernel utils
)
