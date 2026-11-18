#pragma once

/**
 * @file data_stream_port.h
 * @brief A stream port: the interface between whatever sends or takes bytes that are not commands (a module's stream,
 * a provider) and the switch that links them (data_matrix/data_stream.h, DATA_MATRIX.MD). Plain
 * types and one inline call, no dependency, so the lowest layer can take it: a provider includes this, never the switch.
 *
 * The owner EMBEDS the port in its own struct and fills `id` and `pass`; the switch fills `out` when it registers the
 * port and clears it when it unregisters it. In `pass` the owner gets its struct back from `self`
 * (DATA_STREAM_OWNER). The owner sends with data_stream_send(&port, ...), which calls `out`: no link to the switch.
 *
 *   typedef struct { data_stream_port_t port; ... } my_t;
 *   static void my_pass(data_stream_port_t* self, data_stream_port_id_t from, const uint8_t* data, uint32_t len) {
 *     my_t* me = DATA_STREAM_OWNER(self, my_t, port);   // ...
 *   }
 *   static my_t s_me = {.port = {.id = DATA_STREAM_PORT(DATA_STREAM_PORT_MODULE, 0x04), .pass = my_pass}};
 *   data_stream_send(&s_me.port, rec, len);
 *
 * The contract of a port: `pass` is short, never blocks, never sends on its own port, copies what it keeps (`data` is
 * valid for the call only); it runs on the sender's task. data_stream_send only from a task, never from an ISR; a
 * packet with no link is dropped. The port lives as long as it is registered; its owner stops sending before the port
 * is unregistered. The order in which the targets of one port get a packet is no promise.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a port belongs to: a module's own (a fixed id, its stream) or a provider (its provider id). */
typedef enum {
  DATA_STREAM_PORT_MODULE = 0,    /**< A module's stream: ERRORS 0x04, ERRORS_IN 0x05, LOGS 0x03, ... */
  DATA_STREAM_PORT_PROVIDER = 1,  /**< A provider, by its id (sys_data_provider) */
} data_stream_port_kind_e;

/** @brief A port's name: its kind in the high byte, its id in the low one (DATA_STREAM_PORT). Compared as a whole. */
typedef uint16_t data_stream_port_id_t;

/**
 * @brief A port's name from its kind (data_stream_port_kind_e) and its id (the module's stream, the provider's id).
 * @usage DATA_STREAM_PORT(DATA_STREAM_PORT_MODULE, 0x04)   // the errors out
 */
#define DATA_STREAM_PORT(kind, id) ((data_stream_port_id_t)(((kind) << 8) | (id)))

/** @brief The struct a port is embedded in, from the port: `type` is the owner's struct, `member` the port's field. */
#define DATA_STREAM_OWNER(self, type, member) ((type*)(void*)((char*)(self) - offsetof(type, member)))

typedef struct data_stream_port data_stream_port_t;

/**
 * @brief A port's input: bytes for its owner. On the sender's task: short, never blocks, copies what it keeps.
 * @param self  The port (DATA_STREAM_OWNER gives its owner).
 * @param from  The port the packet came out of.
 * @param data  The packet, valid for the call only.
 * @param len   Bytes in data.
 */
typedef void (*data_stream_pass_f)(data_stream_port_t* self, data_stream_port_id_t from, const uint8_t* data,
                                   uint32_t len);

/**
 * @brief A port's output, the switch's: hands the packet unchanged to every port linked from `self`.
 * @param self  The sending port.
 * @param data  The packet, borrowed for the call.
 * @param len   Bytes in data.
 */
typedef void (*data_stream_out_f)(data_stream_port_t* self, const uint8_t* data, uint32_t len);

/** @brief One port, embedded in its owner's struct. */
struct data_stream_port {
  data_stream_port_id_t id;  /**< The owner's: the port's name, unique among the registered ports */
  data_stream_pass_f pass;   /**< The owner's: its input; NULL: the port takes nothing */
  data_stream_out_f out;     /**< The switch's: set while the port is registered, NULL otherwise */
  uint8_t place;             /**< The switch's: the port's place in its table */
};

/**
 * @brief Sends a packet out of a port: to every port linked from it, unchanged, at once. Nothing when the port is not
 * registered or has no link. From a task only.
 * @param port  The owner's port.
 * @param data  The packet, borrowed for the call.
 * @param len   Bytes in data.
 */
static inline void data_stream_send(data_stream_port_t* port, const uint8_t* data, uint32_t len) {
  const data_stream_out_f out = port->out;
  if (out != NULL) {
    out(port, data, len);
  }
}

#ifdef __cplusplus
}
#endif
