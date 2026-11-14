# Common registration for every driver under components/devices/{soc,chips,virtual}/.
# Every driver is linked WHOLE_ARCHIVE (self-registered via SYS_DEVICE_TYPE_DEFINE), has no public
# headers or REQUIRES, never depends on another driver (device_*), and only drivers under
# devices/soc/ may require ESP-IDF peripheral drivers.
macro(runit_device)
    cmake_parse_arguments(_RD "" "" "SRCS;PRIV_REQUIRES" ${ARGN})

    if(_RD_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "${COMPONENT_NAME}: unsupported arguments in runit_device(): ${_RD_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT _RD_SRCS)
        message(FATAL_ERROR "${COMPONENT_NAME}: runit_device() requires SRCS")
    endif()

    string(REGEX REPLACE "/+$" "" _rd_dir "${COMPONENT_DIR}")
    get_filename_component(_rd_parent "${_rd_dir}" DIRECTORY)
    get_filename_component(_rd_tier "${_rd_parent}" NAME)
    if(NOT _rd_tier MATCHES "^(soc|chips|virtual)$")
        message(FATAL_ERROR "${COMPONENT_NAME}: drivers must live under devices/{soc,chips,virtual}/, got '${_rd_tier}'")
    endif()

    foreach(_rd_req IN LISTS _RD_PRIV_REQUIRES)
        if(_rd_req MATCHES "^device_")
            message(FATAL_ERROR "${COMPONENT_NAME}: drivers must depend on contracts, not '${_rd_req}'")
        endif()
        if(NOT _rd_tier STREQUAL "soc" AND _rd_req MATCHES "^(esp_driver_|esp_adc$|driver$|esp_rom$|soc$|hal$)")
            message(FATAL_ERROR "${COMPONENT_NAME}: '${_rd_req}' is only allowed under devices/soc/, not devices/${_rd_tier}/")
        endif()
    endforeach()

    idf_component_register(
        SRCS ${_RD_SRCS}
        PRIV_REQUIRES ${_RD_PRIV_REQUIRES}
        WHOLE_ARCHIVE
    )
endmacro()
