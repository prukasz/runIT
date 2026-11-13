# The device error policy, beside the registry (sys_device_policy.h): it follows the registry's events and answers
# the errors the app's sink hands it, in a task of its own; the registry knows nothing of it. Its commands (router/,
# root key MODULE_ID_POLICY) register themselves, so the component is linked whole.
idf_component_register(
    SRCS "sys_device_policy.c" "router/sys_device_policy_router.c"
    INCLUDE_DIRS "." "router"
    REQUIRES errors router
    PRIV_REQUIRES sys_device sys_event utils freertos log
    WHOLE_ARCHIVE
)
