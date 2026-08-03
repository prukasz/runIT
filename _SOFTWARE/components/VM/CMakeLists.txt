idf_component_register(
    SRCS "core/vm_obj_access.c" "core/vm_alloc.c"
    INCLUDE_DIRS "core"
    REQUIRES sys_errors
)
