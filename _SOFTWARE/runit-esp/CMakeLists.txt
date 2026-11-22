cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH})
    set(ENV{IDF_PATH} "C:/esp/v6.1/esp-idf")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The firmware: a project of its own (its sdkconfig and build/). components/ holds the components, grouped by layer
# (core -> sys -> devices/{soc,chips,virtual} -> vm); what is not moved yet from runit-esp-archive is taken from there
# by folder, one by one. A component is built when its folder is found here.
set(RUNIT_SHARED "${CMAKE_CURRENT_LIST_DIR}/../runit-esp-archive/components")

list(APPEND EXTRA_COMPONENT_DIRS
    "${RUNIT_SHARED}/core/time"
)
list(APPEND EXTRA_COMPONENT_DIRS "components/core")
list(APPEND EXTRA_COMPONENT_DIRS "components/sys")
list(APPEND EXTRA_COMPONENT_DIRS "components/sys/bus")
list(APPEND EXTRA_COMPONENT_DIRS "components/devices/soc")
list(APPEND EXTRA_COMPONENT_DIRS "components/devices/chips")
list(APPEND EXTRA_COMPONENT_DIRS "components/devices/virtual")
list(APPEND EXTRA_COMPONENT_DIRS "components/providers")
list(APPEND EXTRA_COMPONENT_DIRS "components/vm")
list(APPEND EXTRA_COMPONENT_DIRS "components/app")

project(runIT)
