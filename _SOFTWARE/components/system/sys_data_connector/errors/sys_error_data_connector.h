#pragma once

// Error owners for sys_data_connector. sys_errors aggregates this map through its
// include dirs (sys_error_codes.h). This folder holds only error maps, so it
// doesn't expose the module's API to everything that includes sys_error.h.
#define SYS_DATA_CONNECTOR_OWNER_MAP(X) \
  X(OWNER_SYS_DATA_CONNECTOR_BASE, 0xAD00, "OWNER_SYS_DATA_CONNECTOR_BASE")                           \
  X(OWNER_SYS_DATA_CONNECTOR_REGISTER_PROVIDER, 0xAD01, "OWNER_SYS_DATA_CONNECTOR_REGISTER_PROVIDER") \
  X(OWNER_SYS_DATA_CONNECTOR_INIT, 0xAD02, "OWNER_SYS_DATA_CONNECTOR_INIT")                           \
  X(OWNER_SYS_DATA_CONNECTOR_REMOVE, 0xAD03, "OWNER_SYS_DATA_CONNECTOR_REMOVE")                       \
  X(OWNER_SYS_DATA_CONNECTOR_BIND_TX, 0xAD04, "OWNER_SYS_DATA_CONNECTOR_BIND_TX")                     \
  X(OWNER_SYS_DATA_CONNECTOR_UNBIND_TX, 0xAD05, "OWNER_SYS_DATA_CONNECTOR_UNBIND_TX")                 \
  X(OWNER_SYS_DATA_CONNECTOR_BIND_RX, 0xAD06, "OWNER_SYS_DATA_CONNECTOR_BIND_RX")                     \
  X(OWNER_SYS_DATA_CONNECTOR_UNBIND_RX, 0xAD07, "OWNER_SYS_DATA_CONNECTOR_UNBIND_RX")                 \
  X(OWNER_SYS_DATA_CONNECTOR_RECEIVE, 0xAD08, "OWNER_SYS_DATA_CONNECTOR_RECEIVE")
