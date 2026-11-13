# A device that uses another device's pin (a chip's RST, OE, EN, an alert line): take, drive, release in one order. The
# only part of IO that needs the EVENT contract (the pin's events to the owner's ON_EVENT), so only drivers require it.
idf_component_register(
    SRCS "sys_io_claim.c"
    INCLUDE_DIRS "."
    REQUIRES sys_io
    PRIV_REQUIRES sys_device sys_device_event sys_event
)
