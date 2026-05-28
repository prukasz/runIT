#pragma once 

#define IO_OWNER_MAP(X) \
    X(OWNER_IO_MANAGER, 0xe200, "OWNER_IO_MANAGER")\
    X(OWNER_IO_PORT_INIT, 0xe201, "OWNER_IO_PORT_INIT")\
    X(OWNER_IO_PORT_SET, 0xe202, "OWNER_IO_PORT_SET")\
    X(OWNER_IO_PORT_READ, 0xe203, "OWNER_IO_PORT_READ")\
    X(OWNER_IO_PORT_TOGGLE, 0xe204, "OWNER_IO_PORT_TOGGLE")\
    X(OWNER_IO_PORT_CALLBACK, 0xe205, "OWNER_IO_PORT_CALLBACK")\
    X(OWNER_IO_PORT_CONFIGURE, 0xe206, "OWNER_IO_PORT_CONFIGURE")


#define IO_ERROR_MAP(X) \
    X(IO_ERR_BASE, 0xE200, "IO_ERR_BASE")\
    X(IO_ERR_NO_FREE_PORT, 0xE201, "No free IO port available")\
    X(IO_ERR_PORT_INVALID, 0xE202, "Invalid IO port ID")\
    X(IO_ERR_FEATURE_UNSUPPORTED, 0xE203, "IO feature is unsupported")\
    X(IO_ERR_PIN_PROTECTED, 0xE204, "IO pin operation blocked by protection lock")\
    X(IO_ERR_PIN_UNSUPPORTED, 0xE205, "IO pin is unsupported")\
    X(IO_ERR_MODE_UNSUPPORTED, 0xE206, "IO mode is unsupported")\
    X(IO_ERR_UPDATE_FAILED, 0xE207, "IO update failed")\
    X(IO_ERR_PIN_IN_OTHER_USE, 0xE208, "IO pin is already in use")\
    X(IO_ERR_PIN_NOT_CONFIGURED, 0xE209, "IO pin is not configured")\
    X(IO_ERR_DEVICE_NOT_FOUND, 0xE20A, "IO device not found")
