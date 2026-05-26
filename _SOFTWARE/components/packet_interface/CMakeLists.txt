idf_component_register(
    SRCS "interface_dispatcher.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_ringbuf 
    PRIV_REQUIRES runit_kernel utils config_power config_io
)
