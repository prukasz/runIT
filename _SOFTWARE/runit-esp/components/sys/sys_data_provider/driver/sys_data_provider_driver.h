#pragma once

#include <stdbool.h>

#include "cmd.h"
#include "data_matrix.h"
#include "sdkconfig.h"
#include "sys_data_provider.h"
#include "utils.h"

/**
 * @file sys_data_provider_driver.h
 * @brief Driver interface for sys_data_provider (a driver only). A driver's own struct starts with the base,
 * sys_data_provider_t (its field `base`): a stream port and the state bits, nothing else. The struct lives in the
 * registry's slot (at most CONFIG_SYS_DATA_PROVIDER_SIZE bytes, never freed: a send in flight still finds its port):
 * the registry zeroes it, names the port and registers it in the data matrix after create(), unregisters it first at
 * REMOVE. The registry never looks into a packet: the port's input hands it to the type's `in` as
 * it is. Calls into a driver (its `in`, its own ops, its remove) come one at a time, so a driver keeps no lock of its
 * own for them. What comes in on the line the driver sends out of its port itself (data_matrix_send(&p->port, ...),
 * from its task, never from an ISR), the line's state with sys_data_provider_set_up. A driver type registers itself
 * with SYS_DATA_PROVIDER_TYPE_DEFINE.
 *
 * The driver's side of the contract:
 * - `in` and the own ops never CREATE or REMOVE a provider (they run under the registry's locks: it would deadlock).
 * - `in` waits for room on its line a bounded time only (its CONFIG_PROVIDER_*_SEND_WAIT_MS): REMOVE and the own ops
 *   of the provider wait for it.
 * - Its RX side (data_matrix_send on its port, set_up) runs at the same time as `in`: what RX and TX share, the
 *   driver guards itself.
 * - When remove returns, no RX callback of the driver runs any more (it unhooks them synchronously). What the driver
 *   allocated in create it frees in remove; its struct is the registry's.
 * - A packet that was on its way when the provider was removed may reach a provider CREATEd again in the same slot
 *   meanwhile: it is taken as that provider's.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sys_data_provider sys_data_provider_t;

/** @brief The base of every provider: the first field (`base`) of the driver's own struct. */
struct sys_data_provider {
  data_matrix_port_t port;  /**< Its only data interface: named, its input set and registered by the registry */
  uint8_t state;            /**< SYS_DATA_PROVIDER_STATE_* */
};

/**
 * @brief The driver's struct of a port (in its type's `in`).
 * @usage my_t* me = SYS_DATA_PROVIDER_OF(self, my_t);
 */
#define SYS_DATA_PROVIDER_OF(self, type) DATA_MATRIX_OWNER(self, type, base.port)

/** @brief A driver's own op code (its type's ops table): 0xFF in the high byte. */
#define SYS_DATA_PROVIDER_OWN_OP(n) ((uint16_t)(0xFF00u | (n)))

/** @brief One own op of a driver type: its code and its sides (a side NULL: not given); `ctx` is the provider. */
typedef struct {
  uint16_t op;   /**< SYS_DATA_PROVIDER_OWN_OP(n) */
  cmd_f read;    /**< The read side, NULL: none */
  cmd_f write;   /**< The write side, NULL: none */
} sys_data_provider_op_t;

/** @brief A driver type: its cfg size, its create, remove and line input, its own ops. */
typedef struct {
  uint8_t type;      /**< sys_data_provider_type_e */
  uint16_t cfg_size; /**< sizeof its CREATE cfg (packed) */
  /** @brief Makes the instance: its line, p->state (WIRED, FRAMED, UP); an error and nothing is made. */
  err_h (*create)(sys_data_provider_t* p, const void* cfg);
  /** @brief Takes the instance down (its hooks on the line), can't fail; the port is out of the data matrix already. */
  void (*remove)(sys_data_provider_t* p);
  /** @brief The port's input: the packet onto the line with its flags as they are (data_matrix.h). */
  data_matrix_in_f in;
  const sys_data_provider_op_t* ops;  /**< Its own ops (reached by the router only), NULL: none */
  uint16_t op_count;                  /**< Rows in ops */
} sys_data_provider_type_t;

/**
 * @brief Adds a driver type to the registry. SYS_DATA_PROVIDER_TYPE_DEFINE calls it before app_main.
 * @param type  The type, static.
 */
void sys_data_provider_type_register(const sys_data_provider_type_t* type);

/**
 * @brief Defines a driver type and registers it before app_main (a constructor). `prov_t` starts with
 * `sys_data_provider_t base`.
 * @usage SYS_DATA_PROVIDER_TYPE_DEFINE(SYS_DATA_PROVIDER_TYPE_UART, uart_cfg_t, uart_t, uart_create, uart_remove,
 *                                      uart_in, NULL, 0)
 */
#define SYS_DATA_PROVIDER_TYPE_DEFINE(prov_type, cfg_t, prov_t, create_fn, remove_fn, in_fn, ops_ptr, ops_cnt)   \
  _Static_assert(_Alignof(cfg_t) == 1, #cfg_t ": create cfg must be packed");                                   \
  _Static_assert(offsetof(prov_t, base) == 0, #prov_t ": sys_data_provider_t base must be its first field");    \
  _Static_assert(sizeof(prov_t) <= CONFIG_SYS_DATA_PROVIDER_SIZE, #prov_t ": over SYS_DATA_PROVIDER_SIZE");     \
  static const sys_data_provider_type_t U__CONCAT(sys_data_provider__type_, __LINE__) = {                       \
      .type     = (prov_type),                                                                                  \
      .cfg_size = sizeof(cfg_t),                                                                                \
      .create   = (create_fn),                                                                                  \
      .remove   = (remove_fn),                                                                                  \
      .in       = (in_fn),                                                                                      \
      .ops      = (ops_ptr),                                                                                    \
      .op_count = (ops_cnt),                                                                                    \
  };                                                                                                            \
  __attribute__((constructor)) static void U__CONCAT(sys_data_provider__type_register_, __LINE__)(void) {       \
    sys_data_provider_type_register(&U__CONCAT(sys_data_provider__type_, __LINE__));                            \
  }

/**
 * @brief The driver tells that its line delivers frames now, or no more (SYS_DATA_PROVIDER_STATE_UP). From a task,
 * never from an ISR.
 * @param p   The provider.
 * @param up  true: up.
 */
void sys_data_provider_set_up(sys_data_provider_t* p, bool up);

#ifdef __cplusplus
}
#endif
