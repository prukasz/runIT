#include <stdlib.h>
#include <string.h>
#include "enc_sys_errors.h"
#include "runit_device_error_policy.h"
#include "sys_device.h"
#include "sys_error.h"

#undef OWNER
#define OWNER OWNER_SYS_IO_BASE

static unsigned checks;
#define CHECK(x)               \
  do {                         \
    ++checks;                  \
    if (!(x)) return __LINE__; \
  } while (0)
unsigned checks_run(void) {
  return checks;
}

// Small host substitutes. Firmware allocation/dispatch/policy/encoding are real.
void* memset(void* p, int value, size_t n) {
  unsigned char* b = p;
  while (n--) *b++ = value;
  return p;
}
void* memcpy(void* p, const void* q, size_t n) {
  unsigned char*       b = p;
  const unsigned char* a = q;
  while (n--) *b++ = *a++;
  return p;
}
static unsigned char heap[16384] __attribute__((aligned(8)));
static size_t        heap_used;
void*                malloc(size_t n) {
  n = (n + 7) & ~7u;
  if (heap_used + n > sizeof(heap)) return NULL;
  void* p = heap + heap_used;
  heap_used += n;
  return p;
}
void* calloc(size_t n, size_t s) {
  void* p = malloc(n * s);
  if (p) memset(p, 0, n * s);
  return p;
}
void free(void* p) {
  (void)p;
}

static unsigned  sends, actions[256], sequence[32], sequence_count, stops, freezes, scopes;
static bool      latched, fail_action, reenter, churn;
static unsigned  hook_calls, hook_corrupt;
static err_tag_e latched_tag;
void             se_log_init(void) {
}
err_h SE_send(err_h chain) {
  if (chain) ++sends;
  return NULL;
}
bool vm_exec_fault_latch(uint8_t id, uint32_t owner, err_tag_e tag) {
  (void)id;
  (void)owner;
  bool first = !latched;
  if (first) latched_tag = tag;
  latched = true;
  return first;
}
err_h vm_exec_stop(void) {
  ++stops;
  return NULL;
}
err_h sys_actions_invoke(unsigned scope, unsigned action) {
  scopes = scope;
  ++actions[action];
  sequence[sequence_count++ % 32] = action;
  if (reenter) {
    reenter = false;
    SE_EMIT_ERR(ERR_ESP_ERR, .esp_code = ESP_FAIL);
  }
  if (fail_action) return SE_ERR_NEW(ERR_BASE_INVALID_STATE, 0);
  return NULL;
}
err_h sys_io_report_fault(err_h node, err_h chain) {
  (void)chain;
  ++hook_calls;
  if (churn) {
    err_tag_e before = node->tag;
    for (unsigned i = 0; i < 100; ++i) SE_release(SE_ERR_NEW(ERR_BASE_NOT_FOUND, 0));
    if (node->tag != before) ++hook_corrupt;
  }
  return NULL;
}
static err_h install(const void* cfg, void** h) {
  (void)cfg;
  *h = (void*)1;
  return NULL;
}
static err_h freeze(void* h) {
  (void)h;
  ++freezes;
  return NULL;
}
static const sys_device_class_t cls = {.name = "test", .ops = {.install = install, .freeze = freeze}};

int run_tests(void) {
  err_h nodes[SE_MAX_CHAIN_DEPTH];
  bool  complete;
  CHECK(!SE_is_valid_error_ptr((err_h)1));
  CHECK(SE_get_error_root((err_h)1) == NULL);
  CHECK(SE_alloc_bytes((size_t)-1, ERR_NULL_PTR, OWNER) == NULL);
  CHECK(SE_alloc_bytes(1, ERR_MAX_COUNT, OWNER) == NULL);
  err_h    held = SE_ERR_NEW(ERR_ESP_ERR, .esp_code = 1234);
  err_h    fill[40];
  unsigned count = 0;
  while (count < 40 && (fill[count] = SE_alloc_bytes(1, ERR_BASE_NOT_FOUND, OWNER))) ++count;
  CHECK(count == 31);
  CHECK(((err_payload_ERR_ESP_ERR_t*)held->payload)->esp_code == 1234);
  CHECK(SE_WRAP_ERR(held, ERR_DEP_FAILED, 0) == held);
  err_h exhausted = SE_ERR_NEW(ERR_NULL_PTR, 0);
  CHECK(exhausted && exhausted->tag == ERR_BASE_NO_MEM);
  CHECK(SE_is_valid_error_ptr(exhausted));
  SE_release(exhausted);
  for (unsigned i = 0; i < count; ++i) SE_release(fill[i]);
  SE_release(held);
  CHECK(!SE_is_valid_error_ptr(held));
  for (unsigned i = 0; i < 1000; ++i) {
    held = SE_ERR_NEW(ERR_ESP_ERR, .esp_code = 42);
    CHECK(held->tag == ERR_ESP_ERR);
    SE_release(held);
  }

  held             = SE_ERR_NEW(ERR_BASE_NOT_FOUND, 0);
  held->next_cause = held;
  CHECK(SE_collect_chain(held, nodes, &complete) == 1 && !complete);
  CHECK(SE_get_error_root(held) == NULL);
  unsigned sent = sends;
  SE_push_to_handler(held);
  CHECK(sends == sent + 1 && hook_calls == 0);
  held             = SE_ERR_NEW(ERR_BASE_NOT_FOUND, 0);
  err_h second     = SE_WRAP_ERR(held, ERR_DEP_FAILED, 0);
  held->next_cause = second;
  SE_release(second);
  CHECK(!SE_is_valid_error_ptr(held) && !SE_is_valid_error_ptr(second));

  held = NULL;
  for (unsigned i = 0; i < 17; ++i) held = SE_WRAP_ERR(held, ERR_DEP_FAILED, 0);
  uint8_t packet[512];
  size_t  len = 0;
  CHECK(enc_sys_errors_encode_chain(held, packet, sizeof(packet), &len) == NULL);
  CHECK(packet[0] == 16 && packet[1] == 255);
  SE_release(held);
  err_h invalid = enc_sys_errors_encode_chain((err_h)1, packet, sizeof(packet), &len);
  CHECK(invalid != NULL);
  SE_release(invalid);

  for (unsigned id = 0; id < 12; ++id) {
    CHECK(sys_device_install_cfg(&cls, id, NULL, 0) == NULL);
    uint8_t config[5] = {0, id + 1, id + 1, id + 1, id + 1};
    CHECK(sys_device_set_error_handling(id, SYS_DEV_IMPORTANCE_CRITICAL, config) == NULL);
  }
  // Tag-based device identification even when owner is IO; outer first.
  held           = SE_ERR_NEW(ERR_DEV_SUSPENDED, .dev_id = 1);
  held           = SE_WRAP_ERR(held, ERR_DEV_SUSPENDED, .dev_id = 2);
  churn          = true;
  sequence_count = 0;
  SE_push_to_handler(held);
  churn = false;
  CHECK(sequence_count == 2 && sequence[0] == 3 && sequence[1] == 2 && !hook_corrupt);

  // More than eight devices, with a repeated ninth: one action per device.
  memset(actions, 0, sizeof(actions));
  held = SE_ERR_NEW(ERR_DEV_SUSPENDED, .dev_id = 9);
  for (unsigned id = 0; id < 10; ++id) held = SE_WRAP_ERR(held, ERR_DEV_SUSPENDED, .dev_id = id);
  SE_push_to_handler(held);
  for (unsigned id = 0; id < 10; ++id) CHECK(actions[id + 1] == 1);

  CHECK(sys_device_set_error_handling(0, SYS_DEV_IMPORTANCE_NONE, NULL) == NULL);
  held            = SE_ERR_NEW(ERR_ESP_ERR, .esp_code = ESP_FAIL);
  held            = SE_WRAP_ERR(held, ERR_DEV_DEP_FAILED, .dev_id = 0);
  unsigned before = hook_calls;
  SE_push_to_handler(held);
  CHECK(!latched && hook_calls == before);
  // Unattributed critical faults cannot be suppressed by real device zero.
  SE_EMIT_ERR(ERR_NULL_PTR, 0);
  CHECK(latched && stops == 1);

  // A wrapped ESP error on a non-critical device must not trigger system fault
  latched = false;
  unsigned stopped = stops;
  memset(actions, 0, sizeof(actions));
  held = SE_ERR_NEW(ERR_ESP_ERR, .esp_code = ESP_FAIL);
  held = SE_WRAP_ERR(held, ERR_DEV_NOT_FOUND, .dev_id = 1);
  SE_push_to_handler(held);
  CHECK(!latched && stops == stopped && actions[2] == 1);

  latched = false;
  stopped = stops;
  memset(actions, 0, sizeof(actions));
  held = SE_ERR_NEW(ERR_BASE_NOT_FOUND, 0);
  held = SE_WRAP_ERR(held, ERR_DEV_NO_HANDLE, .dev_id = 1);
  held = SE_WRAP_ERR(held, ERR_DEV_NO_HANDLE, .dev_id = 2);
  SE_push_to_handler(held);
  CHECK(latched && stops == stopped + 1 && actions[2] == 1 && actions[3] == 1);

  // A later critical mention supplies classification/context, only one action.
  latched = false;
  memset(actions, 0, sizeof(actions));
  held = SE_ERR_NEW(ERR_DEV_NO_HANDLE, .dev_id = 1);
  held = SE_WRAP_ERR(held, ERR_DEV_SUSPENDED, .dev_id = 1);
  SE_push_to_handler(held);
  CHECK(actions[2] == 1 && latched_tag == ERR_DEV_NO_HANDLE);
  // An ignored deeper device prevents looking through to later critical mentions.
  latched = false;
  memset(actions, 0, sizeof(actions));
  held = SE_ERR_NEW(ERR_DEV_NO_HANDLE, .dev_id = 1);
  held = SE_WRAP_ERR(held, ERR_DEV_DEP_FAILED, .dev_id = 0);
  held = SE_WRAP_ERR(held, ERR_DEV_SUSPENDED, .dev_id = 1);
  SE_push_to_handler(held);
  CHECK(actions[2] == 1 && !latched);
  // Pin tags attribute their device independently of IO domain handling.
  SE_push_to_handler(SE_ERR_NEW(ERR_IO_PIN_UNCONFIGURED, .dev_id = 1, .pin_num = 3));
  CHECK(actions[2] == 2);

  // Explicit NONE is a no-op; invalid levels never index past actions[].
  held = SE_ERR_NEW(ERR_DEV_SUSPENDED, .dev_id = 1);
  CHECK(sys_device_report_error_with_level(1, SYS_DEV_ERR_NONE, held) == NULL);
  invalid = sys_device_report_error_with_level(1, 99, held);
  CHECK(invalid && invalid->tag == ERR_INVALID_VAL_UI32);
  SE_release(invalid);
  SE_release(held);
  uint8_t dynamic[5] = {2, 0, 5, 0, 0};
  CHECK(sys_device_set_error_handling(1, SYS_DEV_IMPORTANCE_CRITICAL, dynamic) == NULL);
  held        = SE_ERR_NEW(ERR_DEV_SUSPENDED, .dev_id = 1);
  reenter     = true;
  fail_action = true;
  stopped     = stops;
  sent        = sends;
  SE_push_to_handler(held);
  CHECK(scopes == SYS_ACTION_SCOPE_DYNAMIC && actions[5] == 1 && stops == stopped && sends >= sent + 2);
  fail_action = false;

  SE_suspend();
  SE_suspend();
  held = SE_ERR_NEW(ERR_NULL_PTR, 0);
  SE_push_to_handler(held);
  CHECK(!SE_is_valid_error_ptr(held));
  SE_resume();
  CHECK(SE_is_suspended());
  SE_resume();
  CHECK(!SE_is_suspended());
  // All consumed chains returned their storage, including response failures.
  count = 0;
  while (count < 40 && (fill[count] = SE_alloc_bytes(1, ERR_BASE_NOT_FOUND, OWNER))) ++count;
  CHECK(count == 32);
  for (unsigned i = 0; i < count; ++i) SE_release(fill[i]);
  return 0;
}
