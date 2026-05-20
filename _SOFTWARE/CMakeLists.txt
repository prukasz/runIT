cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Ignore the VM folder totally
set(EXCLUDE_COMPONENTS "vm")

# Point directly to the folders in your project root
list(APPEND EXTRA_COMPONENT_DIRS 
    "components/status"
    "components/runit_kernel"
    "components/packet_interface"
    "components/mocks"
    "components/managers"
    "components/drivers"
    "components/wrappers"
    "components/utils"
)
project(runIT)

# Disable treating format string warnings as errors
add_compile_options(-Wno-error=format)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=format" APPEND)
