/**
 * @file errors.c
 * @brief The node: making one (err__new), reading it, freeing a chain; the chain's queries and its severity. Registers
 * the core tags (errors/core_errors.h).
 */

#include "errors.h"

#include <assert.h>
#include <string.h>
#include "err_internal.h"

ERR_MODULE_DEFINE(MODULE_ID_CORE, ERR_CORE_MAP)

// Returned by err__new when the pool is empty and there is no cause to fall back on. Never freed (err_free skips it);
// static, so its `next` and `info` are NULL.
static struct err s_pool_empty = {.tag = ERR_POOL_EMPTY};

uint32_t err_tag(err_h e) {
  return e != NULL ? e->tag : 0;
}

err_h err_cause(err_h e) {
  return e != NULL ? e->next : NULL;
}

const void* err_data(err_h e) {
  return e != NULL ? e->data : NULL;
}

err_h err_find(err_h chain, uint32_t tag) {
  uint32_t left = ERR_CHAIN_MAX;
  for (err_h e = chain; e != NULL && left-- > 0; e = e->next) {
    if (e->tag == tag) {
      return e;
    }
  }
  return NULL;
}

#if CONFIG_ERRORS_INFO
err_h err__new(const err_info_t* info, uint32_t tag, const void* data, uint32_t len, err_h cause)
#else
err_h err__new(uint32_t tag, const void* data, uint32_t len, err_h cause)
#endif
{
  assert(data != NULL || len == 0);
  if (len > ERR_DATA_MAX) {
    assert(!"error data longer than ERR_DATA_MAX");
    len = ERR_DATA_MAX;
  }

  err_h e = err_pool_alloc();
  if (e == NULL) {
    return cause != NULL ? cause : &s_pool_empty;
  }

#if CONFIG_ERRORS_INFO
  e->info = info;
#endif
  e->next = cause;
  e->tag = tag;
  if (len > 0) {
    memcpy(e->data, data, len);
  }
  // Rest of the block zeroed, so the node never carries stale bytes.
  memset(e->data + len, 0, ERR_DATA_MAX - len);
  return e;
}

void err_free(err_h chain) {
  uint32_t left = ERR_CHAIN_MAX;

  while (chain != NULL && left-- > 0) {
    err_h next = chain->next;  // read before the block is reused by the pool
    if (chain != &s_pool_empty) {
      err_pool_free(chain);
    }
    chain = next;
  }
}

uint8_t err_sev(err_h e) {
  const err_tag_info_t* info = e != NULL ? err_tag_info(e->tag) : NULL;
  return info != NULL ? info->sev : ERR_SEV_NONE;
}

uint8_t err_chain_sev(err_h chain) {
  uint8_t sev = ERR_SEV_NONE;
  uint32_t left = ERR_CHAIN_MAX;

  for (err_h e = chain; e != NULL && left-- > 0; e = e->next) {
    const uint8_t s = err_sev(e);
    sev = s > sev ? s : sev;
  }
  return sev;
}

uint32_t err_root_tag(err_h chain) {
  uint32_t left = ERR_CHAIN_MAX;
  while (chain != NULL && chain->next != NULL && left-- > 0) {
    chain = chain->next;
  }
  return chain != NULL ? chain->tag : 0;
}

bool err_drop(err_h* err, uint32_t tag) {
  if (*err == NULL || err_root_tag(*err) != tag) {
    return false;
  }
  err_free(*err);
  *err = NULL;
  return true;
}
