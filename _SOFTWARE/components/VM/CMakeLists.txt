# core/ is grouped by layer -- errors, store, obj, block, loader -- but
# every include across the project is by bare filename ("vm_obj.h", not
# "obj/vm_obj.h"). That is deliberate: the grouping is for people reading the
# tree, and regrouping it again later should not touch a single #include line.
# Each group therefore goes on the include path rather than into the paths.
idf_component_register(
    SRCS "core/store/vm_store.c"
         "core/obj/vm_obj_build.c" "core/obj/vm_obj_access.c" "core/obj/vm_obj_dyn.c"
         "core/block/vm_block_build.c"
         "core/loader/vm_loader.c"
    INCLUDE_DIRS "core/errors" "core/store" "core/obj" "core/block" "core/loader"
    REQUIRES sys_errors
)

# The project builds at -Og (CONFIG_COMPILER_OPTIMIZATION_DEBUG), which is the
# right default for driver and bring-up code but the wrong one here: accessor
# resolution passes vm_payload_t and vm_resolved_t by value through several
# layers, and -Og turns every one of those into stack traffic instead of
# keeping them in registers. This is the one component that runs inside a scan
# cycle rather than at boot or on a packet, so it is compiled for speed
# regardless of the project-wide setting. Measured with vm_bench.c -- see the
# benchmark section of VM.MD before changing this.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
