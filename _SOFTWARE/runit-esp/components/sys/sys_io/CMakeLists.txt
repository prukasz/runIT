# The IO contract: the caller's side (sys_io.h: the ops, the frames, the events, the wrappers) and the driver's side
# (driver/sys_io_driver.h: pin state/checks/lock ops for a device with pins, and sys_io_claim_* for a device that uses
# another device's pin).
idf_component_register(
    SRCS "sys_io.c" "driver/sys_io_driver.c"
    INCLUDE_DIRS "." "driver" "errors"
    REQUIRES errors cmd sys_device sys_event
    PRIV_REQUIRES utils sys_device_event
)
