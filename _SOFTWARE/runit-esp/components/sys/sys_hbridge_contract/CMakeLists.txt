# The H-bridge contract: its table, frames and events are the header; the calls are SYS_DEVICE_SET / SYS_DEVICE_GET
# of sys_device. Header only.
idf_component_register(
    INCLUDE_DIRS "."
    REQUIRES sys_device sys_event
)
