# The power contracts (PMON, VREG, PD, SUPPLY): their tables, frames and events in contracts/, the shared errors and
# the one call with logic of its own (sys_power_usb_pd_list_options). WHOLE_ARCHIVE: sys_power_contract.c registers the error
# tags by a constructor; a device may raise ERR_POWER_* without calling into it, and the linker would drop it.
idf_component_register(
    SRCS "sys_power_contract.c"
    INCLUDE_DIRS "." "contracts" "errors"
    REQUIRES errors cmd sys_device sys_event
    WHOLE_ARCHIVE
)
