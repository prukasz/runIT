#include "sys_hbridge.h"

/* The contract is its header (the table, the frames, the events); the calls are SYS_DEVICE_SET / SYS_DEVICE_GET. This
 * source only claims the contract's ID for the whole firmware. */
SYS_DEVICE_CONTRACT_DEFINE(HBRIDGE, 0x09)
