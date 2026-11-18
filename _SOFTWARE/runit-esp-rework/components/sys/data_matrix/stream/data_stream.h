#pragma once

/**
 * @file data_stream.h
 * @brief The stream interface and the switch that connects it (DATA_MATRIX.MD "data_stream"). NOT IN THE BUILD yet:
 * the next data matrix, written beside the current one.
 *
 * A module (the errors, the logs, the events), a provider, a session's channel: anything that sends or takes bytes
 * that are not commands is a PORT. A port has an input (`pass`: the switch hands it bytes) and / or an output
 * (`attach`: the switch gives it the function it sends with). The switch keeps only the ports and the links "output of
 * A -> input of B", and hands every packet over unchanged: no role, no session, no tag, no format, no queue, no task.
 *
 *   module A                      switch                         module B
 *   attach(out, link) <---------  (A registered)
 *   out(link, data, len) ------>  for each link A -> B:  ------> B.pass(ctx, A, data, len)
 *
 * Synchronous: `out` calls the inputs of the linked ports at once, on the sender's task. So every port keeps the
 * contract below; what must wait or be kept is its own business (a provider queues for its line, a module copies into
 * a queue of its own). Since no pass sends on its own output, out never runs inside out: links A -> B and B -> A are
 * safe, and the switch checks no cycle.
 *
 * The contract of a port:
 *  - `pass` is short, never blocks, never sends on its own output (a loop the switch cannot see), copies what it
 *    keeps: `data` is valid for the call only. It runs on the sender's task.
 *  - `out` is called from a task, never from an ISR. A packet with no link is dropped without a word.
 *  - The port struct is the owner's and lives as long as it is registered. The owner stops calling `out` before it
 *    unregisters the port: its place in the switch may go to another port after it.
 *  - The order in which the targets of one output get a packet is no promise.
 */

#include <stdint.h>
#include "errors.h"

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

/** @brief A link: the output of `from` goes to the input of `to`. */
typedef struct __attribute__((packed)) {
  data_stream_port_id_t from;  /**< The port that sends (u16 LE) */
  data_stream_port_id_t to;    /**< The port that takes (u16 LE) */
} data_stream_link_t;

_Static_assert(sizeof(data_stream_link_t) == 4, "data_stream wire size");

/**
 * @brief A port's output: what the switch gives the port to send with. Calls the input of every port linked from it.
 * @param link  What attach gave with it: the port's place in the switch.
 * @param data  The packet, passed on unchanged; borrowed for the call.
 * @param len   Bytes in data.
 */
typedef void (*data_stream_out_f)(void* link, const uint8_t* data, uint32_t len);

/** @brief One port: its name, its input and its output. Static, the owner's. */
typedef struct {
  data_stream_port_id_t id;  /**< The port's name; unique among the registered ports */
  void* ctx;                 /**< The owner's, given back to pass and attach */
  /**
   * @brief The port's input (NULL: the port takes nothing). On the sender's task: short, never blocks, copies.
   * @param ctx   The port's ctx.
   * @param from  The port the packet came out of.
   * @param data  The packet, valid for the call only.
   * @param len   Bytes in data.
   */
  void (*pass)(void* ctx, data_stream_port_id_t from, const uint8_t* data, uint32_t len);
  /**
   * @brief The port's output (NULL: the port sends nothing). Called when the port is registered (out set) and when it
   * is unregistered (out NULL).
   * @param ctx   The port's ctx.
   * @param out   The function to send with, NULL: nothing is linked from the port any more.
   * @param link  The first argument of every out call.
   */
  void (*attach)(void* ctx, data_stream_out_f out, void* link);
} data_stream_port_t;

/**
 * @brief Registers a port. A module's at boot; a provider's when it is made.
 * @param port  The port, static (kept by pointer).
 * @return NULL; ERR_DATA_MATRIX_BAD_ARG (NULL, neither pass nor attach, a name already registered),
 *         ERR_DATA_MATRIX_FULL (CONFIG_DATA_STREAM_PORTS taken)
 */
MUST_USE err_h data_stream_register(const data_stream_port_t* port);

/**
 * @brief Unregisters a port: its links go with it, its attach gets NULL. A provider's when it is removed.
 * @param id  The port's name.
 * @return NULL; ERR_DATA_MATRIX_BAD_ARG (no such port)
 */
MUST_USE err_h data_stream_unregister(data_stream_port_id_t id);

/**
 * @brief Links the output of `from` to the input of `to`. The same link again changes nothing.
 * @param link  The link.
 * @return NULL; ERR_DATA_MATRIX_BAD_ARG (an unknown port, a port to itself, `from` with no output, `to` with no input),
 *         ERR_DATA_MATRIX_FULL (`from` already goes to CONFIG_DATA_STREAM_TARGETS ports)
 */
MUST_USE err_h data_stream_connect(const data_stream_link_t* link);

/**
 * @brief Removes a link; not there: nothing changes.
 * @param link  The link.
 * @return NULL
 */
MUST_USE err_h data_stream_disconnect(const data_stream_link_t* link);

#ifdef __cplusplus
}
#endif
