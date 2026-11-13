# The IO contract: the caller's side (sys_io.h: the ops, the frames, the events, the wrappers) and a device with pins'
# side (driver/sys_io_driver.h: the pin state, its checks, the lock ops). A device that uses another device's pin:
# sys_io_claim, a component of its own.
idf_component_register(
    SRCS "sys_io.c" "driver/sys_io_driver.c"
    INCLUDE_DIRS "." "driver" "errors"
    REQUIRES errors router sys_device sys_event
    PRIV_REQUIRES utils
)
