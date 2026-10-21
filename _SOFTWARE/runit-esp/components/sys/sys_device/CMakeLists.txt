# The registry and everything built on it (contract_*, devices) see only cmd; the router is the registry's
# remote interface (router/), private to this component except for the app registering it.
#
#   registry/   the types, the instances, the calls and the sweeps (sys_device.h)
#   errors/     the error tags and their messages
#   policy/     the error policy (sys_device_policy.h)
#   router/     the remote interface (ROUTER_KEY_DEVICE)
idf_component_register(
    SRCS "registry/sys_device.c" "policy/sys_device_policy.c" "router/sys_device_router.c"
    INCLUDE_DIRS "registry" "errors" "policy" "router"
    REQUIRES errors utils cmd
    PRIV_REQUIRES router freertos log
)
