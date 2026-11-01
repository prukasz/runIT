#pragma once

#include "sys_data_provider.h"

/**
 * @file sys_data_provider_driver.h
 * @brief The provider itself and what its driver (sys_ble, sys_uart) does with it: add, remove, keep max_frame
 * current, deliver what comes in. Only drivers include it. A driver makes a provider by its own create command,
 * fills the struct and adds it; remove takes it out once no one uses it and has the driver free it.
 * @usage p->send = my_send; p->destroy = my_destroy; p->id = id;
 *        p->state = SYS_DATA_PROVIDER_STATE_FRAMED | SYS_DATA_PROVIDER_STATE_WIRED;
 *        ERR_TRY(sys_data_provider_add(p));
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sys_data_provider sys_data_provider_t;

/**
 * @brief A provider. The driver sets `send`, `destroy`, `max_frame`, `driver`, `id` and `state` (FRAMED, WIRED); the
 * rest, and `state`'s other bits, are the registry's, read and changed under its lock only. Widest fields first, so
 * nothing pads; never packed: max_frame is a single word.
 */
struct sys_data_provider {
  /**
   * @brief Sends one whole frame. Thread-safe. With NOWAIT it only puts the frame in its queue: callable from any
   * driver's RX task, its own too, so it never takes a lock that task holds.
   * @param self   This provider.
   * @param data   The frame.
   * @param len    Bytes in `data`, at most max_frame.
   * @param flags  SYS_DATA_PROVIDER_SEND_URGENT: to the front of its queue; SYS_DATA_PROVIDER_SEND_NOWAIT: no wait
   *               for room.
   * @return NULL when queued; ERR_PROV_BUSY when its queue is full (at once with NOWAIT, else after its own bound),
   * else the driver's error.
   */
  MUST_USE err_h (*send)(sys_data_provider_t* self, const uint8_t* data, uint32_t len, uint8_t flags);

  /**
   * @brief Frees what the driver made. Called by sys_data_provider_remove() only.
   * @param self  This provider.
   */
  void (*destroy)(sys_data_provider_t* self);

  volatile uint32_t max_frame;  /**< Largest whole frame now, 0 = down (Frame size): framed, CMD_FRAME_MAX when up;
                                     set before add, then by sys_data_provider_set_max_frame() only */
  void* driver;                 /**< The driver's own state: characteristic, port */
  sys_data_provider_rx_f rx;    /**< The user's (sys_data_provider_open()) */
  void* ctx;                    /**< Given to `rx` */
  sys_data_provider_id_t id;    /**< Its id */
  uint8_t state;                /**< SYS_DATA_PROVIDER_STATE_*: FRAMED, WIRED the driver's, the rest the registry's */
  uint8_t busy;                 /**< Sends in progress */
};

/**
 * @brief Adds a provider the driver made: its id is found from now on.
 * @param p  The provider, the driver's fields set, the rest zero; valid until its destroy.
 * @return NULL when added; ERR_PROV_EXISTS (the id is taken), ERR_PROV_FULL.
 */
MUST_USE err_h sys_data_provider_add(sys_data_provider_t* p);

/**
 * @brief Removes a provider: takes it out, so its id finds nothing, and frees it with its destroy.
 * @param id  The provider.
 * @return NULL when removed; ERR_PROV_NOT_FOUND, ERR_PROV_IN_USE (open, or a send in progress), ERR_PROV_SYSTEM.
 */
MUST_USE err_h sys_data_provider_remove(sys_data_provider_id_t id);

/** @brief Makes every provider added so far the board's own: they can't be removed. Boot step. */
void sys_data_provider_mark_system(void);

/**
 * @brief Sets max_frame (up, down: 0, the MTU of an unframed one) and, when it changed, tells the user (its rx with
 * `data` NULL). The driver's task, never an ISR.
 * @param p    The provider.
 * @param max  The largest whole frame now; 0 = down.
 */
void sys_data_provider_set_max_frame(sys_data_provider_t* p, uint32_t max);

/**
 * @brief Hands what came in to the user's rx (dropped without one). The driver's RX task, never an ISR.
 * @param p     The provider.
 * @param data  One whole frame (the driver joins the fragments first).
 * @param len   Bytes in `data`.
 */
void sys_data_provider_deliver(sys_data_provider_t* p, const uint8_t* data, uint32_t len);

#ifdef __cplusplus
}
#endif
