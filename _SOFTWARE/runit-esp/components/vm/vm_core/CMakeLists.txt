# The VM core: object store, accessors, block layer, supervisor, loader, telemetry, retained values (VM_CORE.MD).
# The palette (the block types) is a separate component, vm_blocks: each block registers itself (block/vm_blocks.h).
# WHOLE_ARCHIVE: the error tags are registered by a constructor in vm_errors.c.
idf_component_register(
    SRCS "errors/vm_errors.c"
         "store/vm_store.c"
         "obj/vm_obj_build.c" "obj/vm_obj_access.c" "obj/vm_obj_access_internal.c" "obj/vm_obj_dyn.c"
         "block/vm_block_build.c" "block/vm_block_run.c" "block/vm_block_helpers.c" "block/vm_blocks.c"
         "exec/vm_event.c" "exec/vm_exec.c" "exec/vm_clock.c" "exec/vm_stats.c" "exec/vm_override.c"
         "loader/vm_loader.c"
         "sub/vm_sub.c"
         "retain/vm_retain.c"
         "router/vm_router.c"
    INCLUDE_DIRS "." "errors" "store" "obj" "block" "exec" "loader" "sub" "retain" "router"
    REQUIRES errors utils cmd router freertos esp_timer esp_ringbuf sys_event sys_connector sys_actions
    PRIV_REQUIRES log nvs_flash
    WHOLE_ARCHIVE
)

# The project builds at -Og, the right default for driver and bring-up code but the wrong one for the hot path
# (accessor resolution passes vm_payload_t and vm_resolved_t by value through several levels), so the core builds at -O2.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
