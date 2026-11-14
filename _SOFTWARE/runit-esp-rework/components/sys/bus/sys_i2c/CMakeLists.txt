# The I2C contract (a bus is a device) and its client (a chip on a bus): sys_i2c.h. No bus is made here: the ESP32-S3's
# controller is the device device_esp32s3_i2c, and any other bus is a device of its own.
idf_component_register(
    SRCS "sys_i2c.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors cmd sys_device
)
