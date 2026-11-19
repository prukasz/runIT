#pragma once

/**
 * @file data_matrix.h
 * @brief The data matrix: the switch that links ports (DATA_MATRIX.MD). A minimal, synchronous implementation: a
 * table of ports and their links and the calls that hand a packet on, on the sender's task; no role, no session, no
 * tag, no format, no queue, no task, no waiting.
 *
 *   module A                         switch                          module B
 *   data_matrix_send(&A.port, ...)  -> for each link A -> B:  ------> B.port.in(&B.port, A, flags, data, len)
 *
 * A port is anything a provider's bytes reach, the commands included: a module's, a provider's, a session's. The
 * owner EMBEDS the port in its own struct and fills `id` and `in`; the switch keeps its place while it is registered.
 * In `in` the owner gets its struct back from `self` (DATA_MATRIX_OWNER).
 *
 *   typedef struct { data_matrix_port_t port; ... } my_t;
 *   static void my_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* data,
 *                     uint32_t len) {
 *     my_t* me = DATA_MATRIX_OWNER(self, my_t, port);   // ...
 *   }
 *   static my_t s_me = {.port = {.id = DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, 0x2A), .in = my_in}};
 *   data_matrix_send(&s_me.port, CMD_SEND_URGENT, rec, len);
 *
 * data_matrix_send hands a packet to every port linked from the sender, data_matrix_send_except to all but one (what a
 * port passes on, never back to `from`): where a port's output goes is its links. data_matrix_send_to hands it to one
 * registered port by its name, with no link: an answer to the `from` a packet came with, a call to the port its
 * caller names.
 *
 * A packet carries its flags (CMD_SEND_*, cmd_types.h): the sender sets them, the switch hands them on unchanged
 * with the bytes, the taker honours what it can (a provider queues by them).
 *
 * The contract of a port: `in` is short, never blocks, copies what it keeps (`data` is valid for the call only); it
 * runs on the sender's task. The one wait: a provider's `in` waits for room on its line, at most its driver's bound,
 * when the sender did not say CMD_SEND_NOWAIT. Sends only from a task, never from an ISR; a packet with no link is dropped. The switch
 * does not wait for sends in flight, so the port's memory and its `in` stay valid after it is unregistered (a send
 * that copied its targets before may still call it): an owner never frees a registered port. Its owner stops sending
 * before the port is unregistered. The order in which the targets of one port get a packet is no promise.
 *
 * A send inside an `in`: a provider's `in` never sends, so every chain ends at a provider. A session's `in` sends on
 * at once (from its provider out of a sub-port, into a sub-port out to its provider); a module's `in` may pass on
 * what it takes (data_matrix_send_except(from)) or send from a callback (the router's call). Modules' ports that send
 * in their `in` are linked with sessions' sub-ports and providers only, never with each other: so no chain is a cycle,
 * and the switch checks none.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cmd_types.h"
#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a port belongs to: a module (its number), a provider (its id) or a session (its provider's id). */
typedef enum {
  DATA_MATRIX_PORT_MODULE = 0,      /**< A module's, by its module number: the router, the errors, the events, ... */
  DATA_MATRIX_PORT_PROVIDER = 1,    /**< A provider, by its id (sys_data_provider) */
  DATA_MATRIX_PORT_SESSION = 0x80,  /**< | the provider's id (below 0x80): a session's ports, by a module's number, 0
                                         the session's own (sys_session) */
} data_matrix_port_kind_e;

/** @brief A port's name: its kind in the high byte, its id in the low one (DATA_MATRIX_PORT). Compared as a whole. */
typedef uint16_t data_matrix_port_id_t;

/**
 * @brief A port's name from its kind (data_matrix_port_kind_e) and its id (a module's number, a provider's id).
 * @usage DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, MODULE_ID_ROUTER)
 *        DATA_MATRIX_PORT(DATA_MATRIX_PORT_SESSION | provider, MODULE_ID_ROUTER)   // a session's commands
 */
#define DATA_MATRIX_PORT(kind, id) ((data_matrix_port_id_t)(((kind) << 8) | (id)))

/** @brief The struct a port is embedded in, from the port: `type` is the owner's struct, `member` the port's field. */
#define DATA_MATRIX_OWNER(self, type, member) ((type*)(void*)((char*)(self) - offsetof(type, member)))

typedef struct data_matrix_port data_matrix_port_t;

/**
 * @brief A port's input: bytes for its owner. On the sender's task: short, never blocks (a provider: at most its
 * bound, without CMD_SEND_NOWAIT), copies what it keeps.
 * @param self   The port (DATA_MATRIX_OWNER gives its owner).
 * @param from   The port the packet came out of.
 * @param flags  The sender's CMD_SEND_*.
 * @param data   The packet, valid for the call only.
 * @param len    Bytes in data.
 */
typedef void (*data_matrix_in_f)(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags,
                                 const uint8_t* data, uint32_t len);

/** @brief One port, embedded in its owner's struct. */
struct data_matrix_port {
  data_matrix_port_id_t id;  /**< The owner's: the port's name, unique among the registered ports */
  data_matrix_in_f in;       /**< The owner's: its input; NULL: the port takes nothing */
  uint8_t place;             /**< The switch's: the port's place in its table while it is registered */
};

/** @brief A link: the output of `from` goes to the input of `to`. */
typedef struct __attribute__((packed)) {
  data_matrix_port_id_t from;  /**< The port that sends (u16 LE) */
  data_matrix_port_id_t to;    /**< The port that takes (u16 LE) */
} data_matrix_link_t;

_Static_assert(sizeof(data_matrix_link_t) == 4, "data_matrix wire size");

/**
 * @brief Registers a port: keeps it and its place. A module's at boot; a provider's by its CREATE.
 * @param port  The port, the owner's (kept by pointer), its id and `in` filled.
 * @return NULL; ERR_DATA_MATRIX_BAD_ARG (NULL, a name already registered), ERR_DATA_MATRIX_FULL
 *         (CONFIG_DATA_MATRIX_PORTS taken)
 */
MUST_USE err_h data_matrix_register(data_matrix_port_t* port);

/**
 * @brief Unregisters a port: its links go with it. A provider's first thing at its REMOVE.
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
 * @brief Sends a packet out of a port: to every port linked from it, unchanged, at once. Nothing when the port is not
 * registered or has no link. From a task only.
 * @param port   The owner's port.
 * @param flags  CMD_SEND_*: how the takers send it on.
 * @param data   The packet, borrowed for the call.
 * @param len    Bytes in data.
 */
void data_matrix_send(data_matrix_port_t* port, uint8_t flags, const uint8_t* data, uint32_t len);

/**
 * @brief Sends a packet out of a port to one registered port by its name, with no link: an answer to `from`, a call
 * to the port its caller names.
 * @param port   The owner's port.
 * @param to     The target's name.
 * @param flags  CMD_SEND_*.
 * @param data   The packet, borrowed for the call.
 * @param len    Bytes in data.
 * @return true when `to` is registered and took it; false: no such port, or it takes nothing.
 */
bool data_matrix_send_to(data_matrix_port_t* port, data_matrix_port_id_t to, uint8_t flags, const uint8_t* data,
                         uint32_t len);

/**
 * @brief Sends a packet out of a port to every port linked from it but one: what a port passes on, never back to the
 * port it came from.
 * @param port    The owner's port.
 * @param except  The port left out (the `from` of what is passed on).
 * @param flags   CMD_SEND_*.
 * @param data    The packet, borrowed for the call.
 * @param len     Bytes in data.
 */
void data_matrix_send_except(data_matrix_port_t* port, data_matrix_port_id_t except, uint8_t flags,
                             const uint8_t* data, uint32_t len);

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
