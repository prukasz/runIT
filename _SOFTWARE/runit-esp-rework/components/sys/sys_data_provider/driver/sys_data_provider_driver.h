#pragma once

#include "sys_data_provider.h"

/**
 * @file sys_data_provider_driver.h
 * @brief Driver interface for sys_data_provider: every driver registers under sys_data_provider_type_e
 * via SYS_DATA_PROVIDER_TYPE_DEFINE with create, remove, and optional k_ops table; create sets p->send and p->state.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sys_data_provider sys_data_provider_t;

struct sys_data_provider {
  MUST_USE err_h (*send)(sys_data_provider_t* self, cmd_in_t frame, uint8_t flags);
  void* driver;              /**< Instance context allocated from ctx_size (or set by driver) */
  sys_data_provider_rx_f rx; /**< Owner callback (sys_data_provider_open()) */
  void* ctx;                 /**< Context passed to `rx` */
  uint8_t id;                /**< Provider ID (0..0xFE) */
  uint8_t type;              /**< sys_data_provider_type_e */
  uint8_t state;             /**< SYS_DATA_PROVIDER_STATE_* */
  uint8_t busy;              /**< Active sys_data_provider_send() calls in progress */
  data_stream_port_t port;   /**< The provider as a stream port (sys_data_provider_set_port_hooks); the registry's */
};

typedef struct {
  uint16_t op;
  cmd_f read;
  cmd_f write;
  uint8_t flags;  /**< SYS_DATA_PROVIDER_OP_* */
} sys_data_provider_op_t;

typedef struct {
  uint8_t type;       /**< sys_data_provider_type_e */
  uint16_t cfg_size;
  uint16_t ctx_size;
  err_h (*create)(sys_data_provider_t* p, void* ctx, const void* cfg);
  err_h (*remove)(sys_data_provider_t* p, void* ctx);
  const sys_data_provider_op_t* ops;
  uint16_t op_count;
} sys_data_provider_type_t;

void sys_data_provider_type_register(const sys_data_provider_type_t* type);

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

static inline bool sys_data_provider_spare_refused(uint8_t flags, uint32_t used, uint32_t need, uint32_t cap) {
  return (flags & CMD_SEND_SPARE) != 0 && used + need > cap / 2u;
}

void sys_data_provider_set_up(sys_data_provider_t* p, bool up);
void sys_data_provider_deliver(sys_data_provider_t* p, const uint8_t* data, uint32_t len);

#ifdef __cplusplus
}
#endif
