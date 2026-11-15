# Shared registration helper for self-registering provider drivers (components/providers/*).
# Enforces zero public header (no INCLUDE_DIRS), zero public REQUIRES, no cross-provider deps,
# and WHOLE_ARCHIVE linking so __attribute__((constructor)) type registration always runs.
# Must be a macro so idf_component_register() runs in the component CMakeLists.txt scope.
macro(runit_provider)
    cmake_parse_arguments(_RP "" "" "SRCS;PRIV_INCLUDE_DIRS;PRIV_REQUIRES;REQUIRES;INCLUDE_DIRS" ${ARGN})
    if(_RP_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "${CMAKE_CURRENT_LIST_FILE}: unexpected arguments: ${_RP_UNPARSED_ARGUMENTS}")
    endif()
    if(_RP_REQUIRES)
        message(FATAL_ERROR "${CMAKE_CURRENT_LIST_FILE}: a provider driver must not have public REQUIRES (use PRIV_REQUIRES)")
    endif()
    if(_RP_INCLUDE_DIRS)
        message(FATAL_ERROR "${CMAKE_CURRENT_LIST_FILE}: a provider driver has no public INCLUDE_DIRS")
    endif()
    foreach(_rp_dep IN LISTS _RP_PRIV_REQUIRES)
        if(_rp_dep MATCHES "^provider_")
            message(FATAL_ERROR "${CMAKE_CURRENT_LIST_FILE}: a provider driver must not depend on another provider (${_rp_dep})")
        endif()
    endforeach()
    idf_component_register(
        SRCS ${_RP_SRCS}
        PRIV_INCLUDE_DIRS ${_RP_PRIV_INCLUDE_DIRS}
        PRIV_REQUIRES ${_RP_PRIV_REQUIRES}
        WHOLE_ARCHIVE
    )
endmacro()
