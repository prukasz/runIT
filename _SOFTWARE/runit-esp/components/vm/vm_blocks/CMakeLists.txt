# The VM palette: the block types (VM_BLOCKS.MD). Each block file declares its type with VM_BLOCK_TYPE_DEFINE and
# registers before app_main; nothing references a block, so the component is linked WHOLE_ARCHIVE.
#
#   core/                 logic blocks (expressions, flow, state, actions, events)
#   device-contract/      one block per device contract, on one engine (vm_block_device.c): NOT BUILT yet, they still call
#                         the old device API (sys_device_call, the old op names)
idf_component_register(
    SRCS "core/expr_core/vm_expr_core.c"
         "core/expr/vm_block_expr.c" "core/expr_bit/vm_block_expr_bit.c"
         "core/if/vm_block_if.c" "core/switch/vm_block_switch.c" "core/for/vm_block_for.c"
         "core/set/vm_block_set.c" "core/clone/vm_block_clone.c" "core/action/vm_block_action.c"
         "core/on_event/vm_block_on_event.c" "core/edge/vm_block_edge.c" "core/timer/vm_block_timer.c"
         "core/latch/vm_block_latch.c" "core/periodic/vm_block_periodic.c" "core/map/vm_block_map.c"
    INCLUDE_DIRS "core/expr_core" "core/branch_core"
                 "core/expr" "core/expr_bit" "core/if" "core/switch" "core/for" "core/set" "core/clone" "core/action"
                 "core/on_event" "core/edge" "core/timer" "core/latch" "core/periodic" "core/map"
    REQUIRES vm_core errors utils sys_event sys_device sys_session sys_watch
    WHOLE_ARCHIVE
)

# Same reason as the core: the blocks are the hot path.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
