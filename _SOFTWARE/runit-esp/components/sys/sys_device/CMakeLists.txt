# The registry and everything built on it (contract_*, devices) see only cmd; the router is the registry's
# remote interface (router/), private to this component except for the app registering it.
#
#   registry/   the types, the instances, the calls and the sweeps (sys_device.h)
#   errors/     the error module and the error policy
#   router/     the remote interface (ROUTER_KEY_DEVICE)
idf_component_register(
    SRCS "errors/sys_device_errors.c" "registry/sys_device.c" "router/sys_device_router.c"
    INCLUDE_DIRS "registry" "errors" "router"
    REQUIRES errors utils cmd
    PRIV_REQUIRES router freertos log
)
