# The registry's remote interface (root key MODULE_ID_SYS_DEVICE): commands from the app and from other boards. It registers itself
# (ROUTER_MODULE_REGISTER): nothing references it, so it is linked whole. Required by whoever builds the frame
# (sys_device_router_hdr_t); drivers never see the router.
idf_component_register(
    SRCS "sys_device_router.c"
    INCLUDE_DIRS "."
    REQUIRES router sys_device
    WHOLE_ARCHIVE
)
