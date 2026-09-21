# Collect all C source files from subdirectories (registry/, servo/, hbridge/, etc.)
file(GLOB_RECURSE FEATURE_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/registry/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/servo/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/hbridge/*.c"
)

# Collect all include directories
set(FEATURE_INCS
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/registry/include"
    "${CMAKE_CURRENT_LIST_DIR}/servo/include"
    "${CMAKE_CURRENT_LIST_DIR}/hbridge/include"
)

idf_component_register(
    SRCS ${FEATURE_SRCS}
    INCLUDE_DIRS ${FEATURE_INCS}
    REQUIRES
        sys_errors
        sys_io
        sys_hbridge
        sys_device
        callbacks
        utils
)
