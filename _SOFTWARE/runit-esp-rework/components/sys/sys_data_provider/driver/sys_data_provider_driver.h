#pragma once

#include "sys_data_provider.h"

/**
 * @file sys_data_provider_driver.h
 * @brief The provider itself and what its driver (sys_ble, sys_uart) does with it: add, remove, keep up or down
 * current, deliver what comes in. Only drivers include it. A driver makes a provider by its own create command, fills
 * the struct and adds it; remove takes it out once no one uses it and has the driver free it.
 * @usage p->send = my_send; p->destroy = my_destroy; p->id = id; p->link = SYS_DATA_PROVIDER_LINK_UART;
 *        p->state = SYS_DATA_PROVIDER_STATE_SYSTEM | SYS_DATA_PROVIDER_STATE_WIRED | SYS_DATA_PROVIDER_STATE_FRAMED;
 *        ERR_TRY(sys_data_provider_add(p));
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sys_data_provider sys_data_provider_t;

/**
 * @brief A provider. The driver sets `send`, `destroy`, `driver`, `id`, `link` and `state` (SYSTEM, WIRED, FRAMED,
 * NO_RX, UP); the rest, and `state`'s other bits, are the registry's, read and changed under its lock only. Widest
 * fields first, so nothing pads.
 */
struct sys_data_provider {
  /**
   * @brief Sends one whole frame, given in two parts: the driver copies `head` and then `body` into its queue as one
   * frame. Thread-safe. With NOWAIT it only puts the frame in its queue: callable from any driver's RX task, its own
   * too, so it never takes a lock that task holds.
   * @param self     This provider.
   * @param head     The first part (NULL when head_len is 0).
   * @param head_len Bytes in `head`.
   * @param body     The second part (NULL when body_len is 0).
   * @param body_len Bytes in `body`. The two together are 1..CMD_FRAME_MAX; a raw provider's driver refuses what its
   *                 link can't take.
   * @param flags    SYS_DATA_PROVIDER_SEND_URGENT: to the front of its queue; _NOWAIT: no wait for room; _SPARE:
   *                 ERR_PROV_BUSY when the frame would fill the queue past half (sys_data_provider_spare_refused).
   * @return NULL when queued; ERR_PROV_BUSY when its queue is full (at once with NOWAIT, else after its own bound) or,
   *         with SPARE, past half; else the driver's error.
   */
  MUST_USE err_h (*send)(sys_data_provider_t* self, const uint8_t* head, uint32_t head_len, const uint8_t* body,
                         uint32_t body_len, uint8_t flags);

  /**
   * @brief Frees what the driver made. Called by sys_data_provider_remove() only.
   * @param self  This provider.
   */
  void (*destroy)(sys_data_provider_t* self);

  void* driver;               /**< The driver's own state: characteristic, port */
  sys_data_provider_rx_f rx;  /**< The user's (sys_data_provider_open()) */
  void* ctx;                  /**< Given to `rx` */
  uint8_t id;                 /**< Its id, whoever made it chose it: a name, no number means anything */
  uint8_t link;               /**< sys_data_provider_link_e: shown in LIST */
  uint8_t state;              /**< SYS_DATA_PROVIDER_STATE_*: OPEN the registry's, the rest the driver's */
  uint8_t busy;               /**< Sends in progress */
};

/**
 * @brief The one rule of SYS_DATA_PROVIDER_SEND_SPARE, for a driver's send: the frame is refused when, queued, it would
 * leave less than half of the queue free. In the driver's own unit (frames or bytes), read under its queue's lock.
 * @param[in] flags The send's flags
 * @param[in] used  What the queue holds now
 * @param[in] need  What this frame takes
 * @param[in] cap   What the queue holds at most
 * @return true: answer ERR_PROV_BUSY, the frame is not queued
 * @usage if (sys_data_provider_spare_refused(flags, queued, 1, TX_FRAMES)) { return ERR_NEW(ERR_PROV_BUSY, ...); }
 */
static inline bool sys_data_provider_spare_refused(uint8_t flags, uint32_t used, uint32_t need, uint32_t cap) {
  return (flags & SYS_DATA_PROVIDER_SEND_SPARE) != 0 && used + need > cap / 2u;
}

/**
 * @brief Adds a provider the driver made: its id is found from now on.
 * @param[in] p The provider, the driver's fields set, the rest zero; valid until its destroy.
 * @return NULL when added; ERR_CMD_BAD_ARG (no send), ERR_PROV_EXISTS (the id is taken), ERR_PROV_FULL.
 */
MUST_USE err_h sys_data_provider_add(sys_data_provider_t* p);

/**
 * @brief Removes a provider: takes it out, so its id finds nothing, and frees it with its destroy.
 * @param[in] id The provider.
 * @return NULL when removed; ERR_PROV_NOT_FOUND, ERR_PROV_IN_USE (open, or a send in progress), ERR_PROV_SYSTEM.
 */
MUST_USE err_h sys_data_provider_remove(uint8_t id);

/**
 * @brief Sets whether the provider is up and, when it changed, tells the user (its rx with `data` NULL). The driver's
 * task, never an ISR.
 * @param[in] p  The provider.
 * @param[in] up It delivers frames now.
 */
void sys_data_provider_set_up(sys_data_provider_t* p, bool up);

/**
 * @brief Hands what came in to the user's rx (dropped without one). The driver's RX task, never an ISR.
 * @param[in] p    The provider.
 * @param[in] data One whole frame (the driver joins the fragments first).
 * @param[in] len  Bytes in `data`.
 */
void sys_data_provider_deliver(sys_data_provider_t* p, const uint8_t* data, uint32_t len);

#ifdef __cplusplus
}
#endif
