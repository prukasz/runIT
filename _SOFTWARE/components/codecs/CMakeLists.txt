idf_component_register(
    INCLUDE_DIRS "decoders"
    REQUIRES
        sys_errors
        sys_interface
        sys_data_connector
        sys_actions
        ble
        sys_device
        sys_io
        sys_power
        sys_hbridge
        sys_event
        VM
        devices
        features
)
