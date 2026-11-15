cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH})
    set(ENV{IDF_PATH} "C:/esp/v6.1/esp-idf")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The reworked firmware: a project of its own (its sdkconfig and build/), parallel to runit-esp. components/ holds only
# the new components, grouped by layer like runit-esp (core -> sys -> devices/{soc,chips,virtual} -> vm); what the
# rework still shares with the firmware is taken from there by folder, one by one. A component is built when its folder
# is found here. The step a component joins is in REWORK.md.
set(RUNIT_SHARED "${CMAKE_CURRENT_LIST_DIR}/../runit-esp/components")

list(APPEND EXTRA_COMPONENT_DIRS
    "${RUNIT_SHARED}/core/utils"
    "${RUNIT_SHARED}/core/time"
)
list(APPEND EXTRA_COMPONENT_DIRS "components/core")
list(APPEND EXTRA_COMPONENT_DIRS "components/sys")
list(APPEND EXTRA_COMPONENT_DIRS "components/sys/bus")
list(APPEND EXTRA_COMPONENT_DIRS "components/devices/soc")
list(APPEND EXTRA_COMPONENT_DIRS "components/devices/chips")
list(APPEND EXTRA_COMPONENT_DIRS "components/devices/virtual")
list(APPEND EXTRA_COMPONENT_DIRS "components/providers")
list(APPEND EXTRA_COMPONENT_DIRS "components/app")

project(runIT_rework)
