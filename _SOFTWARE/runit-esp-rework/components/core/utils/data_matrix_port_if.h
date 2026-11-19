#pragma once

/**
 * @file data_matrix_port_if.h
 * @brief The port interface of the data matrix: a stream port, between whatever sends or takes bytes that are not
 * commands (a module's stream, a provider) and the switch that links them (data_matrix/data_matrix.h,
 * DATA_MATRIX.MD). Plain types and one inline call, no dependency, so the lowest layer can take it: a module includes
 * this, never the switch.
 *
 * The owner EMBEDS the port in its own struct and fills `id` and `pass`; the switch fills `out` when it registers the
 * port and clears it when it unregisters it. In `pass` the owner gets its struct back from `self`
 * (DATA_MATRIX_OWNER). The owner sends with data_matrix_send(&port, ...), which calls `out`: no link to the switch.
 *
 *   typedef struct { data_matrix_port_t port; ... } my_t;
 *   static void my_pass(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* data,
 *                       uint32_t len) {
 *     my_t* me = DATA_MATRIX_OWNER(self, my_t, port);   // ...
 *   }
 *   static my_t s_me = {.port = {.id = DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, 0x04), .pass = my_pass}};
 *   data_matrix_send(&s_me.port, CMD_SEND_URGENT, rec, len);
 *
 * A packet carries its flags (CMD_SEND_*, cmd_types.h): the sender sets them, the switch hands them on unchanged
 * with the bytes, the taker honours what it can (a provider queues by them).
 *
 * The contract of a port: `pass` is short, never blocks, never sends on its own port, copies what it keeps (`data` is
 * valid for the call only); it runs on the sender's task. data_matrix_send only from a task, never from an ISR; a
 * packet with no link is dropped. The switch is minimal and synchronous: it does not wait for sends in flight, so the
 * port's memory and its `pass` stay valid after it is unregistered (a send that copied its targets before may still
 * call it): an owner never frees a registered port. Its owner stops sending before the port is unregistered. The order
 * in which the targets of one port get a packet is no promise.
 */

#include <stddef.h>
#include <stdint.h>
#include "cmd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a port belongs to: a module's own (a fixed id, its stream) or a provider (its provider id). */
typedef enum {
  DATA_MATRIX_PORT_MODULE = 0,    /**< A module's stream: ERRORS 0x04, ERRORS_IN 0x05, LOGS 0x03, ... */
  DATA_MATRIX_PORT_PROVIDER = 1,  /**< A provider, by its id (sys_data_provider) */
} data_matrix_port_kind_e;

/** @brief A port's name: its kind in the high byte, its id in the low one (DATA_MATRIX_PORT). Compared as a whole. */
typedef uint16_t data_matrix_port_id_t;

/**
 * @brief A port's name from its kind (data_matrix_port_kind_e) and its id (the module's stream, the provider's id).
 * @usage DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, 0x04)   // the errors out
 */
#define DATA_MATRIX_PORT(kind, id) ((data_matrix_port_id_t)(((kind) << 8) | (id)))

/** @brief The struct a port is embedded in, from the port: `type` is the owner's struct, `member` the port's field. */
#define DATA_MATRIX_OWNER(self, type, member) ((type*)(void*)((char*)(self) - offsetof(type, member)))

typedef struct data_matrix_port data_matrix_port_t;

/**
 * @brief A port's input: bytes for its owner. On the sender's task: short, never blocks, copies what it keeps.
 * @param self  The port (DATA_MATRIX_OWNER gives its owner).
 * @param from   The port the packet came out of.
 * @param flags  The sender's CMD_SEND_*.
 * @param data   The packet, valid for the call only.
 * @param len    Bytes in data.
 */
typedef void (*data_matrix_pass_f)(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags,
                                   const uint8_t* data, uint32_t len);

/**
 * @brief A port's output, the switch's: hands the packet unchanged to every port linked from `self`.
 * @param self   The sending port.
 * @param flags  CMD_SEND_*, handed on unchanged.
 * @param data   The packet, borrowed for the call.
 * @param len    Bytes in data.
 */
typedef void (*data_matrix_out_f)(data_matrix_port_t* self, uint8_t flags, const uint8_t* data, uint32_t len);

/** @brief One port, embedded in its owner's struct. */
struct data_matrix_port {
  data_matrix_port_id_t id;  /**< The owner's: the port's name, unique among the registered ports */
  data_matrix_pass_f pass;   /**< The owner's: its input; NULL: the port takes nothing */
  data_matrix_out_f out;     /**< The switch's: set while the port is registered, NULL otherwise */
  uint8_t place;             /**< The switch's: the port's place in its table */
};

/**
 * @brief Sends a packet out of a port: to every port linked from it, unchanged, at once. Nothing when the port is not
 * registered or has no link. From a task only.
 * @param port   The owner's port.
 * @param flags  CMD_SEND_*: how the takers send it on.
 * @param data   The packet, borrowed for the call.
 * @param len    Bytes in data.
 */
static inline void data_matrix_send(data_matrix_port_t* port, uint8_t flags, const uint8_t* data, uint32_t len) {
  const data_matrix_out_f out = port->out;
  if (out != NULL) {
    out(port, flags, data, len);
  }
}

#ifdef __cplusplus
}
#endif
