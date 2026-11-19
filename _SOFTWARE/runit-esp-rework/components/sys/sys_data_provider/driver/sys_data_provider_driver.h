#pragma once

#include <stdbool.h>

#include "data_stream_port.h"
#include "sys_data_provider.h"
#include "utils.h"

/**
 * @file sys_data_provider_driver.h
 * @brief Driver interface for sys_data_provider (a driver only). A provider IS a stream port (its first field): the
 * registry names it and registers it in the data matrix after create(), and unregisters it first at REMOVE. The driver
 * sets the port's input in create() (`p->port.pass`: the packet onto its line, with its flags as they are:
 * CMD_SEND_NOWAIT, no room, dropped at once; without it the driver waits for room up to its limit, on the sender's
 * task; a packet the line does not take is lost). In it the driver gets the provider back with SYS_DATA_PROVIDER_OF.
 * What comes in on the line goes out with sys_data_provider_deliver, the line's state with sys_data_provider_set_up. A
 * driver type registers itself with SYS_DATA_PROVIDER_TYPE_DEFINE.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sys_data_provider sys_data_provider_t;

/** @brief One provider: a stream port first (its input the driver's), then the driver's instance, the registry's. */
struct sys_data_provider {
  data_stream_port_t port;  /**< First: the provider is this port. The registry names it, create() sets its pass */
  void* driver;             /**< Instance context allocated from ctx_size (or set by the driver) */
  uint8_t id;               /**< Provider ID (0..0xFE) */
  uint8_t type;             /**< sys_data_provider_type_e */
  uint8_t state;            /**< SYS_DATA_PROVIDER_STATE_* */
};

/**
 * @brief The provider of a port's input (the port is its first field).
 * @usage const my_t* me = SYS_DATA_PROVIDER_OF(self)->driver;
 */
#define SYS_DATA_PROVIDER_OF(self) DATA_STREAM_OWNER(self, sys_data_provider_t, port)

/** @brief One own op of a driver type: its code and its sides (a side NULL: not given). */
typedef struct {
  uint16_t op;   /**< SYS_DATA_PROVIDER_OWN_OP(n) */
  cmd_f read;    /**< The read side, NULL: none */
  cmd_f write;   /**< The write side, NULL: none */
} sys_data_provider_op_t;

/** @brief A driver type: its cfg and instance sizes, its create and remove, its own ops. */
typedef struct {
  uint8_t type;      /**< sys_data_provider_type_e */
  uint16_t cfg_size; /**< sizeof its CREATE cfg (packed) */
  uint16_t ctx_size; /**< sizeof its instance, allocated (and freed) by the registry; 0: none */
  /** @brief Makes the instance: sets p->port.pass and p->state (WIRED, FRAMED, UP); an error and nothing is made. */
  err_h (*create)(sys_data_provider_t* p, void* ctx, const void* cfg);
  /** @brief Takes the instance down (its hooks on the line); the port is out of the data matrix already. */
  err_h (*remove)(sys_data_provider_t* p, void* ctx);
  const sys_data_provider_op_t* ops;  /**< Its own ops (reached by the router only), NULL: none */
  uint16_t op_count;                  /**< Rows in ops */
} sys_data_provider_type_t;

/**
 * @brief Adds a driver type to the registry. SYS_DATA_PROVIDER_TYPE_DEFINE calls it before app_main.
 * @param type  The type, static.
 */
void sys_data_provider_type_register(const sys_data_provider_type_t* type);

/**
 * @brief Defines a driver type and registers it before app_main (a constructor).
 * @usage SYS_DATA_PROVIDER_TYPE_DEFINE(SYS_DATA_PROVIDER_TYPE_UART, uart_cfg_t, uart_t, uart_create, uart_remove,
 *                                      NULL, 0)
 */
#define SYS_DATA_PROVIDER_TYPE_DEFINE(prov_type, cfg_t, ctx_t, create_fn, remove_fn, ops_ptr, ops_cnt)     \
  _Static_assert(_Alignof(cfg_t) == 1, #cfg_t ": create cfg must be packed");                              \
  static const sys_data_provider_type_t U__CONCAT(sys_data_provider__type_, __LINE__) = {                  \
      .type     = (prov_type),                                                                             \
      .cfg_size = sizeof(cfg_t),                                                                           \
      .ctx_size = sizeof(ctx_t),                                                                           \
      .create   = (create_fn),                                                                             \
      .remove   = (remove_fn),                                                                             \
      .ops      = (ops_ptr),                                                                               \
      .op_count = (ops_cnt),                                                                               \
  };                                                                                                       \
  __attribute__((constructor)) static void U__CONCAT(sys_data_provider__type_register_, __LINE__)(void) {  \
    sys_data_provider_type_register(&U__CONCAT(sys_data_provider__type_, __LINE__));                       \
  }

/**
 * @brief Whether a packet with CMD_SEND_SPARE is refused: it would take the queue past half.
 * @param flags  The packet's CMD_SEND_*.
 * @param used   Bytes in the queue now.
 * @param need   Bytes the packet takes.
 * @param cap    The queue's size.
 * @return true: drop it.
 */
static inline bool sys_data_provider_spare_refused(uint8_t flags, uint32_t used, uint32_t need, uint32_t cap) {
  return (flags & CMD_SEND_SPARE) != 0 && used + need > cap / 2u;
}

/**
 * @brief The driver tells that its line delivers frames now, or no more (SYS_DATA_PROVIDER_STATE_UP).
 * @param p   The provider.
 * @param up  true: up.
 */
void sys_data_provider_set_up(sys_data_provider_t* p, bool up);

/**
 * @brief The driver hands what came in on its line: out of the provider's port, to the ports linked from it
 * (data_stream_send; nothing while the port is not registered). From the driver's task, never from an ISR.
 * @param p     The provider.
 * @param data  The bytes, borrowed for the call.
 * @param len   Bytes in data.
 */
void sys_data_provider_deliver(sys_data_provider_t* p, const uint8_t* data, uint32_t len);

#ifdef __cplusplus
}
#endif
