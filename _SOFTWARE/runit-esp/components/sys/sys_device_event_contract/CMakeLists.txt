# The EVENT contract: a device that listens to events (ON_EVENT) and the bridge that delivers them
# (sys_device_event_subscribe). Required by the drivers that listen only.
idf_component_register(
    SRCS "sys_device_event_contract.c"
    INCLUDE_DIRS "."
    REQUIRES sys_device sys_event
)
