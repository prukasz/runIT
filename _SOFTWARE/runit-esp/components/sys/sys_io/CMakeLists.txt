idf_component_register(
    SRCS "sys_io.c" "sys_pins.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors cmd sys_device
    PRIV_REQUIRES sys_event
)
