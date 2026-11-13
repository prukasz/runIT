# The H-bridge contract: its table, frames and events are the header; the calls are SYS_DEVICE_SET / SYS_DEVICE_GET
# of sys_device. The source only claims the contract's ID (SYS_DEVICE_CONTRACT_DEFINE). WHOLE_ARCHIVE: nothing
# references it, and the linker would drop the claim.
idf_component_register(
    SRCS "sys_hbridge.c"
    INCLUDE_DIRS "."
    REQUIRES sys_device sys_event
    WHOLE_ARCHIVE
)
