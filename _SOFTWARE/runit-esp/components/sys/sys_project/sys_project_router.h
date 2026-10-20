#pragma once

#include "router.h"

/* Ops under ROUTER_KEY_PROJECT (SYS_PROJECT.MD "Remote"). Never renumber. */
#define SYS_PROJECT_OP_BEGIN   0x01u
#define SYS_PROJECT_OP_WRITE   0x02u
#define SYS_PROJECT_OP_COMMIT  0x03u
#define SYS_PROJECT_OP_ABORT   0x04u
#define SYS_PROJECT_OP_INFO    0x05u
#define SYS_PROJECT_OP_RUN     0x06u
#define SYS_PROJECT_OP_ERASE   0x07u
#define SYS_PROJECT_OP_OPTIONS 0x08u
#define SYS_PROJECT_OP_SECTION 0x10u  // a frame of the stream only: opens a section
#define SYS_PROJECT_OP_MARK    0x11u  // first frame of the project record: bank, CRC and length of the stream (a no-op when run)

ROUTER_MODULE_DECLARE(sys_project_router);
