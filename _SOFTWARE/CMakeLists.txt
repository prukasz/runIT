cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Ignore the VM folder totally

# Point directly to the folders in your project root
list(APPEND EXTRA_COMPONENT_DIRS 
    "components/system"
    "components/devices"
    "components/runit"
    "components/utils"
    "components/sys_errors"
    "components/codecs"
)
project(runIT)
