# The device registry: what callers (sys_device.h), drivers (sys_device_driver.h) and the app's modules
# (sys_device_hooks.h) see. The rest is private: no other component reaches the slots or the call path.
#
#   slots/      the device table: a lock, the flags, the resources and the dependency masks per ID
#   registry/   the types, CREATE, the registry's ops (SUSPEND, RESUME, REMOVE, INFO, OPS), resources, dependencies
#   call/       a call: interceptor -> device -> caller; the task's nesting and held events; the sweep
#   remote/     devices of another board: CREATE_REMOTE and the remote hook
#   errors/     the error tags and their messages
#   events/     the events the registry publishes
#
# The router (sys_device_router), the EVENT contract (sys_device_event), a device's timer (sys_device_timer) and the
# error policy (sys_device_policy) are components of their own. Spec: SYS_DEVICE.MD.
idf_component_register(
    SRCS "slots/sys_device_slots.c" "registry/sys_device_registry.c" "call/sys_device_call.c"
         "remote/sys_device_remote.c"
    INCLUDE_DIRS "." "errors" "events"
    PRIV_INCLUDE_DIRS "slots" "registry" "remote"
    REQUIRES errors router sys_event
    PRIV_REQUIRES utils freertos
)
