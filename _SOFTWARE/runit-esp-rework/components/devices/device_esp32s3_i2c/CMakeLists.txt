# The ESP32-S3's I2C controller as a bus device: one source, nothing includes it (no INCLUDE_DIRS). WHOLE_ARCHIVE: the
# type registers itself (SYS_DEVICE_TYPE_DEFINE) and nothing references it.
idf_component_register(
    SRCS "device_esp32s3_i2c.c"
    PRIV_REQUIRES errors router sys_device sys_i2c sys_io sys_io_claim esp_driver_i2c
    WHOLE_ARCHIVE
)
