cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH})
    set(ENV{IDF_PATH} "C:/esp/v6.1/esp-idf")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Components are grouped by layer (each group depends only on the ones above it):
#   components/core/<name>             foundation: utils, errors, cmd, router (no hardware, no modules)
#   components/sys/<sys_module>        system modules and the contracts devices implement
#   components/devices/device_<chip>   one driver per chip
#   components/vm/vm_*                 the program runtime (VM core, later the palette)
#   components/app/runit               the application: boot sequence, safe state, error policy, board routing
list(APPEND EXTRA_COMPONENT_DIRS "components/core")
list(APPEND EXTRA_COMPONENT_DIRS "components/sys")
list(APPEND EXTRA_COMPONENT_DIRS "components/devices")
list(APPEND EXTRA_COMPONENT_DIRS "components/vm")
list(APPEND EXTRA_COMPONENT_DIRS "components/app")

project(runIT)
