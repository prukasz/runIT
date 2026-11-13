# A device's timer: a board timer whose expiry is a step of its owner (sys_device_step) and which the registry stops
# with the device (sys_device_timer.h). Required by the drivers that run something later or every so often.
idf_component_register(
    SRCS "sys_device_timer.c"
    INCLUDE_DIRS "."
    REQUIRES sys_device timer
    PRIV_REQUIRES errors
)
