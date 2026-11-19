#pragma once

/* Between router_queue.c (the queue, the port) and router_request.c (this board's calls). Not on the public include
   path. */

#include <stdint.h>

#include "cmd.h"
#include "data_matrix.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The router's port (ROUTER_PORT): its answers and this board's calls go out of it. */
extern data_matrix_port_t router_port;

/** @brief An ANSWER that came in on the router's port from `from`: it ends this board's call with its seq, if any. */
void router_request_answer(data_matrix_port_id_t from, const cmd_hdr_t* hdr, const uint8_t* body, uint32_t len);

/** @brief Starts the calls' timer (their timeouts). router_init. */
void router_request_init(void);

#ifdef __cplusplus
}
#endif
