#pragma once
/*
One owner for all device files (`#define OWNER OWNER_DEVICE`). A device's errors say which device raised them
through the device id the dispatcher adds (ERR_DEV_DEP_FAILED / ERR_DEV_INSTALL_FAILED {dev_id}); the type of
that device is the class name registered under that id, so a per-type owner would only repeat it.
*/

#define PROVIDER_OWNER_MAP(X) X(OWNER_DEVICE, 0xD000, "OWNER_DEVICE")
