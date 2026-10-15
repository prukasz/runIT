#pragma once

#include "router.h"

/* Ops under ROUTER_KEY_ACTIONS (SYS_ACTIONS.MD "Remote"). Never renumber. */
#define SYS_ACTIONS_OP_RUN 0x01u
#define SYS_ACTIONS_OP_RECORD_START 0x02u
#define SYS_ACTIONS_OP_RECORD_STOP 0x03u
#define SYS_ACTIONS_OP_BEGIN 0x04u
#define SYS_ACTIONS_OP_WRITE 0x05u
#define SYS_ACTIONS_OP_COMMIT 0x06u
#define SYS_ACTIONS_OP_READ 0x07u
#define SYS_ACTIONS_OP_INFO 0x08u
#define SYS_ACTIONS_OP_LIST 0x09u
#define SYS_ACTIONS_OP_REMOVE 0x0Au
#define SYS_ACTIONS_OP_ABORT 0x0Bu
#define SYS_ACTIONS_OP_RUN_ALL 0x0Du  // RUN that keeps going after a failing frame
#define SYS_ACTIONS_OP_REMOVE_ALL 0x0Eu  // every stored record (sys_actions_remove_all)

ROUTER_MODULE_DECLARE(sys_actions_router);
