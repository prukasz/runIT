/**
 * @file data_stream_router.c
 * @brief The data matrix's commands (data_stream_router.h): CONNECT, DISCONNECT and LIST, each one call of the switch.
 */

#include "data_stream_router.h"

ROUTER_WRITE(DATA_STREAM, OP_CONNECT) {
  return data_stream_connect(args);
}

ROUTER_WRITE(DATA_STREAM, OP_DISCONNECT) {
  return data_stream_disconnect(args);
}

// LIST: u8 links, then the links, written straight into the answer.
ROUTER_READ(DATA_STREAM, OP_LIST) {
  ERR_CHECK(out->cap >= 1, ERR_CMD_OUT_FULL, .need = 1, .cap = (uint16_t)out->cap);
  const uint32_t room = (out->cap - 1) / sizeof(data_stream_link_t);
  const uint32_t n = data_stream_links((data_stream_link_t*)(void*)(out->data + 1), room);
  const uint32_t need = 1 + n * sizeof(data_stream_link_t);
  ERR_CHECK(n <= room, ERR_CMD_OUT_FULL, .need = (uint16_t)need, .cap = (uint16_t)out->cap);
  out->data[0] = (uint8_t)n;
  out->len = need;
  return NULL;
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY_W(DATA_STREAM, OP_CONNECT),
    ROUTER_ENTRY_W(DATA_STREAM, OP_DISCONNECT),
    ROUTER_ENTRY_R(DATA_STREAM, OP_LIST),
};

ROUTER_MODULE(data_stream_router, k_ops)
ROUTER_MODULE_REGISTER(MODULE_ID_DATA_MATRIX, data_stream_router)
