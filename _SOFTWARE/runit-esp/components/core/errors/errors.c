#include "errors.h"

#include "err_pool.h"

ERR_MODULE_DEFINE(ERR_MOD_CORE, "core", ERR_CORE_MAP)

#if CONFIG_ERRORS_INFO
static const err_info_t s_pool_empty_info = {
    ERR__INFO_FILE
#if CONFIG_ERRORS_INFO_FUNC
    .func = "err_new",
#endif
    ERR__INFO_LINE
};
#endif

// Returned by err_new when the pool is empty and there is no cause to fall
// back on. Never freed; its `next` is always NULL.
static err_t s_pool_empty = {
#if CONFIG_ERRORS_INFO
    .info = &s_pool_empty_info,
#endif
    .next = NULL,
    .tag = ERR_POOL_EMPTY,
    .sev = ERR_SEV_DEFAULT,
};

#if CONFIG_ERRORS_INFO
err_h err_new(const err_info_t* info, uint32_t tag, uint8_t sev, const void* data, uint32_t len, err_h cause)
#else
err_h err_new(uint32_t tag, uint8_t sev, const void* data, uint32_t len, err_h cause)
#endif
{
  assert(data != NULL || len == 0);
  if (sev > ERR_SEV_CRITICAL && sev != ERR_SEV_DEFAULT) {
    assert(!"error severity out of range");
    sev = ERR_SEV_DEFAULT;
  }
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
  e->sev = sev;
  if (len > 0) {
    memcpy(e->data, data, len);
  }
  // Rest of the block zeroed, so the node never carries stale bytes.
  memset(e->data + len, 0, ERR_DATA_MAX - len);
  return e;
}

void err_free(err_h chain) {
  // A well-formed chain is never longer than the pool; the limit only stops a
  // corrupted (cyclic) chain from looping forever.
  uint32_t left = CONFIG_ERRORS_POOL_BLOCKS + 1;

  while (chain != NULL && left-- > 0) {
    err_h next = chain->next;  // read before the block is reused by the pool
    if (chain != &s_pool_empty) {
      err_pool_free(chain);
    }
    chain = next;
  }
}

static err_sink_f s_sink;

void err_set_sink(err_sink_f fn) {
  s_sink = fn;
}

void err_report_ex(err_h chain, const char* log_ctx, const void* origin) {
  if (chain == NULL) {
    return;
  }
  err_log_ctx(chain, log_ctx);
  if (s_sink != NULL) {
    s_sink(chain, origin);
  }
  err_free(chain);
}

void err_report(err_h chain) {
  err_report_ex(chain, NULL, NULL);
}

uint8_t err_sev(err_h e) {
  if (e == NULL) {
    return ERR_SEV_NONE;
  }
  if (e->sev != ERR_SEV_DEFAULT) {
    return e->sev;
  }
  const err_tag_info_t* info = err_tag_info(e->tag);
  return info != NULL ? info->sev : ERR_SEV_NONE;
}

uint8_t err_chain_sev(err_h chain) {
  uint8_t sev = ERR_SEV_NONE;
  uint32_t left = CONFIG_ERRORS_POOL_BLOCKS + 1;  // cycle guard, as in err_free

  for (err_h e = chain; e != NULL && left-- > 0; e = e->next) {
    uint8_t s = err_sev(e);
    if (s > sev) {
      sev = s;
    }
    if (e->sev != ERR_SEV_DEFAULT) {
      break;  // this level's verdict covers everything beneath it
    }
  }
  return sev;
}
