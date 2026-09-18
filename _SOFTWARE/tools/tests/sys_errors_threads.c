#include "sys_error.h"
#define OWNER OWNER_SYS_ERRORS_BASE
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif
// Called simultaneously through ctypes (which releases the Python GIL).
EXPORT int stress_pool(unsigned worker) {
  for (unsigned iteration = 0; iteration < 10000; ++iteration) {
    if (SE_is_suspended()) return 3;
    SE_suspend();
    SE_suspend();
    SE_resume();
    if (!SE_is_suspended()) return 4;
    err_h chain = NULL;
    for (unsigned i = 0; i < 4; ++i) {
      chain = SE_WRAP_ERR(chain, ERR_ESP_ERR, .esp_code = (int)(worker * 100000 + iteration));
    }
    err_h  nodes[SE_MAX_CHAIN_DEPTH];
    bool   complete;
    size_t n = SE_collect_chain(chain, nodes, &complete);
    if (!complete || n != 4) {
      SE_release(chain);
      return 1;
    }
    for (size_t i = 0; i < n; ++i) {
      if (((err_payload_ERR_ESP_ERR_t*)nodes[i]->payload)->esp_code != (int)(worker * 100000 + iteration)) {
        SE_release(chain);
        return 2;
      }
    }
    SE_release(chain);
    SE_resume();
    if (SE_is_suspended()) return 5;
  }
  return 0;
}
