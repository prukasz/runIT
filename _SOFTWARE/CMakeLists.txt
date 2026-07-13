cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXCLUDE_COMPONENTS "vm" "vm_new" "runit_kernel" "interface" "device_ads7128")

# Point directly to the folders in your project root
list(APPEND EXTRA_COMPONENT_DIRS 
    "components/status"
    "components/utils"
    "components/system/ble"
    "components/system/sys_device"
    "components/system/sys_i2c"
    "components/system/sys_io"
    "components/system/sys_power"
    "components/system/sys_interface"
    "components/system/sys_h_bridge"
    "components/drivers/a_ble_driver"
    "components/system/error_handler"
    "components/devices"
)
project(runIT)
