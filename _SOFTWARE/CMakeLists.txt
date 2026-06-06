cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Ignore the VM folder totally
set(EXCLUDE_COMPONENTS "vm")

# Point directly to the folders in your project root
list(APPEND EXTRA_COMPONENT_DIRS 
    "components/status"
    "components/runit_kernel"
    "components/interface"
    "components/mocks"
    "components/managers"
    "components/drivers"
    "components/providers"
    "components/utils"
    "components/features"
    "components/vm_demo"
    "components/vm_new"
)
project(runIT)

