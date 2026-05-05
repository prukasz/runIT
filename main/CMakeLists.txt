set(srcs
    "main.c"
    "../components/drivers_manager/manager_ble/manager_ble.c"
    "../components/drivers_abstraction/a_ble_driver/a_ble.c"
    "../components/drivers_abstraction/a_ble_driver/gap.c"
    "../components/drivers_abstraction/a_ble_driver/gatt_svc.c"
    "../components/drivers_manager/manager_i2c/manager_i2c.c"
    "../components/interface/interface_dispatcher.c"
    "../components/mocks/tca/tca6424a_mock.c"
    "../components/drivers/tca6424a/tca6424a.c"
    "../components/drivers/ina3221/ina3221.c"
    "../components/drivers_manager/wrappers/tca6424a/tca6424a_wrapper.c"
    "../components/drivers_manager/wrappers/ina3221/ina3221_wrapper.c"
)

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS
        "."
        "../components/drivers_manager/manager_ble/include"
        "../components/drivers_abstraction/a_ble_driver/include"
        "../components/status/include"
        "../components/drivers_manager/manager_i2c/include"
        "../components/interface/include"
        "../components/mocks/tca/include"
        "../components/drivers/tca6424a/include"
        "../components/drivers/ina3221/include"
        "../components/drivers_manager/wrappers/tca6424a/include"
        "../components/drivers_manager/wrappers/ina3221/include"
    REQUIRES bt nvs_flash driver
)

                    