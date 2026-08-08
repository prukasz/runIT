idf_component_register(SRCS "runit.c" "vm_selftest.c" "vm_bench.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos log device_gpio_esp device_pca9685 sys_i2c sys_device sys_io sys_errors sys_power device_tca6424a device_tps55289 device_ina3221 device_dac53202 device_ap33772s device_ads7128 ble sys_interface codecs sys_actions VM)

# Matches the VM component's own -O2 (see components/VM/CMakeLists.txt). The
# benchmark's raw-C baseline lives in this file, so compiling it at the
# project's -Og while the code it measures runs at -O2 would understate every
# ratio -- the floor has to be built the same way as what it is a floor for.
set_source_files_properties(vm_bench.c PROPERTIES COMPILE_OPTIONS "-O2")
