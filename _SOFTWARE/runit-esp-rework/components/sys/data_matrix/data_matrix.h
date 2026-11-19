#pragma once

/**
 * @file data_matrix.h
 * @brief The data matrix: the switch that links stream ports (DATA_MATRIX.MD).
 *
 * A port (core/utils/data_matrix_port_if.h) is anything that sends or takes bytes that are not commands: a module's
 * stream, a provider. The switch keeps only the registered ports and, per port, the ports its output goes to, and
 * hands every packet over unchanged: no role, no session, no tag, no format, no queue, no task.
 *
 *   module A                         switch                          module B
 *   data_matrix_send(&A.port, ...)  -> for each link A -> B:  ------> B.port.pass(&B.port, A, flags, data, len)
 *
 * Synchronous: a send calls the inputs of the linked ports at once, on the sender's task; every port keeps the
 * contract in data_matrix_port_if.h. Since no pass sends on its own port, a send never runs inside a send: links A -> B
 * and B -> A are safe, and the switch checks no cycle.
 */

#include <stdint.h>
#include "data_matrix_port_if.h"
#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A link: the output of `from` goes to the input of `to`. */
typedef struct __attribute__((packed)) {
  data_matrix_port_id_t from;  /**< The port that sends (u16 LE) */
  data_matrix_port_id_t to;    /**< The port that takes (u16 LE) */
} data_matrix_link_t;

_Static_assert(sizeof(data_matrix_link_t) == 4, "data_matrix wire size");

/**
 * @brief Registers a port: sets its `out` and its place. A module's at boot; a provider's by its CREATE.
 * @param port  The port, the owner's (kept by pointer), its id and pass filled.
 * @return NULL; ERR_DATA_MATRIX_BAD_ARG (NULL, a name already registered), ERR_DATA_MATRIX_FULL
 *         (CONFIG_DATA_MATRIX_PORTS taken)
 */
MUST_USE err_h data_matrix_register(data_matrix_port_t* port);

/**
 * @brief Unregisters a port: its links go with it, its `out` is cleared. A provider's first thing at its REMOVE.
 * @param port  The port.
 * @return NULL; ERR_DATA_MATRIX_BAD_ARG (not registered)
 */
MUST_USE err_h data_matrix_unregister(data_matrix_port_t* port);

/**
 * @brief Links the output of `from` to the input of `to`. The same link again changes nothing.
 * @param link  The link.
 * @return NULL; ERR_DATA_MATRIX_BAD_ARG (an unknown port, a port to itself, `to` with no input),
 *         ERR_DATA_MATRIX_FULL (`from` already goes to CONFIG_DATA_MATRIX_TARGETS ports)
 */
MUST_USE err_h data_matrix_connect(const data_matrix_link_t* link);

/**
 * @brief Removes a link; not there: nothing changes.
 * @param link  The link.
 * @return NULL
 */
MUST_USE err_h data_matrix_disconnect(const data_matrix_link_t* link);

/**
 * @brief The links, port by port: each output's targets.
 * @param out  Where the links go, may be NULL when max is 0.
 * @param max  Room in out, in links.
 * @return How many links there are; only the first `max` are written.
 */
uint32_t data_matrix_links(data_matrix_link_t* out, uint32_t max);

#ifdef __cplusplus
}
#endif
