# The power contracts (PMON, VREG, PD, SUPPLY): their tables, frames and events in contracts/, the shared errors and
# the one call with logic of its own (sys_power_usb_pd_list_options). WHOLE_ARCHIVE: sys_power.c registers the error
# tags by a constructor and claims the contracts' IDs (SYS_DEVICE_CONTRACT_DEFINE); a device may raise ERR_POWER_* or
# implement a contract without calling into it, and the linker would drop it.
idf_component_register(
    SRCS "sys_power.c"
    INCLUDE_DIRS "." "contracts" "errors"
    REQUIRES errors router sys_device sys_event
    WHOLE_ARCHIVE
)
