/**
 * @file data_stream_router.c
 * @brief The switch's commands (data_stream_router.h): CONNECT and DISCONNECT, each one call of the switch. NOT IN THE
 * BUILD yet.
 */

#include "data_stream_router.h"

ROUTER_WRITE(DATA_STREAM, OP_CONNECT) {
  return data_stream_connect(args);
}

ROUTER_WRITE(DATA_STREAM, OP_DISCONNECT) {
  return data_stream_disconnect(args);
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY_W(DATA_STREAM, OP_CONNECT),
    ROUTER_ENTRY_W(DATA_STREAM, OP_DISCONNECT),
};

ROUTER_MODULE(data_stream_router, k_ops)
ROUTER_MODULE_REGISTER(MODULE_ID_DATA_MATRIX, data_stream_router)
