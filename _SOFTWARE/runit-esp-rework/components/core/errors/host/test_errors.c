/* Host tests of the errors component: the real code; only the platform (clock, log, spinlock, sdkconfig) is replaced.
 * Run: python test_host/run.py errors   (HOST_TESTS.MD) */
#include <math.h>
#include <pthread.h>
#include <string.h>
#include "err_log.h"
#include "err_record.h"
#include "err_sink.h"
#include "unity.h"
#include "host_assert.h"
#include "errors.h"
#include "err_internal.h"
#include "host_mocks.h"

/* A module of the test's own (900 is outside the firmware's 0x01..0xFE range). */
enum { MODULE_ID_TEST = 900 };
#define T_MAP(X, M)                                                                                                     \
  X(M, T_PLAIN, 1, ERR_SEV_LOW,      {})                                                                                \
  X(M, T_PAIR,  2, ERR_SEV_HIGH,     { uint8_t a; uint16_t b; })                                                        \
  X(M, T_CRIT,  3, ERR_SEV_CRITICAL, { uint8_t x; })                                                                    \
  X(M, T_MED,   4, ERR_SEV_MEDIUM,   {})
ERR_TAGS_DECLARE(MODULE_ID_TEST, T_MAP)
ERR_MODULE_DEFINE(MODULE_ID_TEST, T_MAP)

void setUp(void) {
  host_log_clear();
  host_time_set(0);
}
void tearDown(void) {}

static int pool_free_count(void) {
  err_h held[CONFIG_ERRORS_POOL_BLOCKS];
  int n = 0;
  while (n < CONFIG_ERRORS_POOL_BLOCKS && (held[n] = err_pool_alloc()) != NULL) n++;
  for (int i = 0; i < n; i++) err_pool_free(held[i]);
  return n;
}
/* Every block is back: nothing of the test leaked. */
#define ASSERT_POOL_FULL() TEST_ASSERT_EQUAL_INT(CONFIG_ERRORS_POOL_BLOCKS, pool_free_count())

static void test_new_has_tag_and_data(void) {
  err_h e = ERR_NEW(T_PAIR, .a = 7, .b = 0x1234);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_ERR_TAG(T_PAIR, e);
  TEST_ASSERT_NULL(e->next);
  TEST_ASSERT_EQUAL_UINT8(7, ERR_DATA(e, T_PAIR)->a);
  TEST_ASSERT_EQUAL_HEX16(0x1234, ERR_DATA(e, T_PAIR)->b);
  err_free(e);
  ASSERT_POOL_FULL();
}

static void test_wrap_chain_order_root_and_severity(void) {
  err_h e = ERR_WRAP(ERR_WRAP(ERR_NEW(T_PLAIN), T_PAIR, .a = 1), T_CRIT, .x = 9);
  TEST_ASSERT_ERR_TAG(T_CRIT, e);
  TEST_ASSERT_EQUAL_HEX32(T_PAIR, e->next->tag);
  TEST_ASSERT_EQUAL_HEX32(T_PLAIN, e->next->next->tag);
  TEST_ASSERT_EQUAL_HEX32(T_PLAIN, err_root_tag(e));
  TEST_ASSERT_EQUAL_UINT8(ERR_SEV_CRITICAL, err_sev(e));
  TEST_ASSERT_EQUAL_UINT8(ERR_SEV_HIGH, err_chain_sev(e->next));
  TEST_ASSERT_EQUAL_UINT8(ERR_SEV_NONE, err_chain_sev(NULL));
  TEST_ASSERT_EQUAL_HEX32(0, err_root_tag(NULL));
  err_free(e);
  ASSERT_POOL_FULL();
}

/* The opaque node is read through err_tag / err_cause / err_data / err_find; NULL reads as nothing. */
static void test_accessors_and_find(void) {
  err_h e = ERR_WRAP(ERR_WRAP(ERR_NEW(T_PLAIN), T_PAIR, .a = 5, .b = 6), T_CRIT, .x = 9);
  TEST_ASSERT_EQUAL_HEX32(T_CRIT, err_tag(e));
  TEST_ASSERT_EQUAL_HEX32(T_PAIR, err_tag(err_cause(e)));
  TEST_ASSERT_NULL(err_cause(err_cause(err_cause(e))));
  TEST_ASSERT_EQUAL_UINT8(9, ERR_DATA(e, T_CRIT)->x);
  err_h pair = err_find(e, T_PAIR);
  TEST_ASSERT_EQUAL_PTR(err_cause(e), pair);
  TEST_ASSERT_EQUAL_UINT8(5, ERR_DATA(pair, T_PAIR)->a);
  TEST_ASSERT_EQUAL_HEX16(6, ERR_DATA(pair, T_PAIR)->b);
  TEST_ASSERT_EQUAL_PTR(e, err_find(e, T_CRIT));     /* the outermost node counts */
  TEST_ASSERT_NULL(err_find(e, T_MED));              /* no node has it */
  TEST_ASSERT_NULL(err_find(NULL, T_PLAIN));
  TEST_ASSERT_EQUAL_HEX32(0, err_tag(NULL));
  TEST_ASSERT_NULL(err_cause(NULL));
  TEST_ASSERT_NULL(err_data(NULL));
  err_free(e);
  ASSERT_POOL_FULL();
}

static void test_drop_takes_away_only_the_cause_it_names(void) {
  err_h e = ERR_WRAP(ERR_NEW(T_PLAIN), T_PAIR, .a = 1);  /* the cause is T_PLAIN, wrapped */
  TEST_ASSERT_FALSE(err_drop(&e, T_PAIR));              /* the outer tag is not the cause: kept */
  TEST_ASSERT_ERR_TAG(T_PAIR, e);
  TEST_ASSERT_TRUE(err_drop(&e, T_PLAIN));
  TEST_ASSERT_NULL(e);
  ASSERT_POOL_FULL();                                   /* the whole chain freed */
  err_h none = NULL;
  TEST_ASSERT_FALSE(err_drop(&none, T_PLAIN));          /* no error: nothing to take */
  TEST_ASSERT_NULL(none);
}

static void test_pool_exhaustion(void) {
  err_h held[CONFIG_ERRORS_POOL_BLOCKS];
  for (int i = 0; i < CONFIG_ERRORS_POOL_BLOCKS; i++) held[i] = ERR_NEW(T_PLAIN);
  err_h over = ERR_NEW(T_PLAIN);
  TEST_ASSERT_EQUAL_HEX32(ERR_POOL_EMPTY, over->tag);  /* the static node, never NULL */
  err_h wrapped = ERR_WRAP(held[0], T_PAIR, .a = 1);
  TEST_ASSERT_EQUAL_PTR(held[0], wrapped);  /* ERR_WRAP: the cause comes back unchanged */
  err_free(over);                      /* the static node is not a pool block: no assert */
  for (int i = 0; i < CONFIG_ERRORS_POOL_BLOCKS; i++) err_free(held[i]);
  ASSERT_POOL_FULL();
}

static err_h fails(int v) {
  ERR_CHECK(v > 0, T_PAIR, .a = (uint8_t)v);
  return NULL;
}
static err_h try_it(int v) {
  ERR_TRY(fails(v));
  return NULL;
}
static void test_try_check_esp(void) {
  TEST_ASSERT_NULL(try_it(1));
  err_h e = try_it(0);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_ERR_TAG(T_PAIR, e);
  err_free(e);
  TEST_ASSERT_NULL(ERR_ESP(ESP_OK));
  e = ERR_ESP(ESP_FAIL);
  TEST_ASSERT_ERR_TAG(ERR_ESP_ERR, e);
  TEST_ASSERT_EQUAL_INT(ESP_FAIL, ERR_DATA(e, ERR_ESP_ERR)->code);
  err_free(e);
}

static void test_keep_first(void) {
  err_h acc = NULL;
  ERR_KEEP_FIRST(acc, ERR_NEW(T_PLAIN));
  ERR_KEEP_FIRST(acc, ERR_NEW(T_CRIT, .x = 1));
  TEST_ASSERT_EQUAL_HEX32(T_PLAIN, acc->tag);
  TEST_ASSERT_NULL(acc->next);
  err_free(acc);
  ASSERT_POOL_FULL();  /* the second was freed, not leaked */
}

static void test_record_internal_bytes(void) {
  host_time_set(0x0807060504030201ull);
  err_h e = ERR_WRAP(ERR_NEW(T_PLAIN), T_PAIR, .a = 0xAA, .b = 0xBBCC);
  uint8_t buf[64];
  uint32_t n = err_record(e, buf, sizeof(buf));
  const uint8_t want[] = {
      ERR_RECORD_SRC_INTERNAL,                      /* source */
      1, 2, 3, 4, 5, 6, 7, 8,                       /* stamp u64 LE */
      2,                                            /* node count */
      0x02, 0x00, 0x84, 0x03, 3, 0xAA, 0xCC, 0xBB,  /* T_PAIR: tag 900<<16|2 LE, len 3, packed payload */
      0x01, 0x00, 0x84, 0x03, 0,                    /* T_PLAIN, no payload */
  };
  TEST_ASSERT_EQUAL_UINT32(sizeof(want), n);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, buf, sizeof(want));
  err_free(e);
}

static void test_record_cut_by_capacity(void) {
  err_h e = ERR_WRAP(ERR_NEW(T_PLAIN), T_PAIR, .a = 1, .b = 2);
  uint8_t buf[64];
  uint32_t n = err_record(e, buf, 9 + 1 + 8);  /* header + count + room for the outer node only */
  TEST_ASSERT_EQUAL_UINT32(9 + 1 + 8, n);
  TEST_ASSERT_EQUAL_HEX8(1 | 0x80, buf[9]);  /* one node, cut flag */
  TEST_ASSERT_EQUAL_UINT32(0, err_record(e, buf, 9));  /* no room for header + count */
  TEST_ASSERT_EQUAL_UINT32(0, err_record(e, NULL, 64));
  err_free(e);
}

static void test_record_cmd_cuts_request(void) {
  uint8_t req[100];
  for (int i = 0; i < 100; i++) req[i] = (uint8_t)i;
  err_h e = ERR_NEW(T_PLAIN);
  uint8_t buf[200];
  uint32_t n = err_record_cmd(0x11, 0x2233, 0x05, req, sizeof(req), e, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_HEX8(ERR_RECORD_SRC_COMMAND, buf[0]);
  const uint8_t hdr[] = {0x11, 0x33, 0x22, 0x05};  /* from, seq LE, flags */
  TEST_ASSERT_EQUAL_HEX8_ARRAY(hdr, buf + 9, sizeof(hdr));
  TEST_ASSERT_EQUAL_HEX8(CONFIG_ERRORS_RECORD_REQ_MAX | ERR_RECORD_REQ_CUT, buf[13]);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(req, buf + 14, CONFIG_ERRORS_RECORD_REQ_MAX);
  TEST_ASSERT_EQUAL_UINT32(14 + CONFIG_ERRORS_RECORD_REQ_MAX + 1 + 5, n);
  n = err_record_cmd(1, 1, 0, req, 4, e, buf, sizeof(buf));  /* short request: whole, no cut bit */
  TEST_ASSERT_EQUAL_HEX8(4, buf[13]);
  err_free(e);
}

static void test_record_board(void) {
  err_h e = ERR_NEW(T_PLAIN);
  uint8_t buf[32];
  uint32_t n = err_record_board(0x42, e, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_HEX8(ERR_RECORD_SRC_BOARD, buf[0]);
  TEST_ASSERT_EQUAL_HEX8(0x42, buf[9]);  /* the board's id */
  TEST_ASSERT_EQUAL_HEX8(1, buf[10]);    /* one node */
  TEST_ASSERT_EQUAL_UINT32(10 + 1 + 5, n);
  err_free(e);
}

static void test_record_round_trip(void) {
  err_h e = ERR_WRAP(ERR_WRAP(ERR_NEW(T_PLAIN), T_PAIR, .a = 5, .b = 600), T_CRIT, .x = 3);
  uint8_t buf[128];
  uint32_t n = err_record(e, buf, sizeof(buf));
  err_h back = NULL;
  err_h rc = err_record_parse(buf, n, &back);
  TEST_ASSERT_NULL(rc);
  TEST_ASSERT_NOT_NULL(back);
  TEST_ASSERT_ERR_TAG(T_CRIT, back);
  TEST_ASSERT_EQUAL_UINT8(3, ERR_DATA(back, T_CRIT)->x);
  TEST_ASSERT_EQUAL_HEX32(T_PAIR, back->next->tag);
  TEST_ASSERT_EQUAL_UINT8(5, ERR_DATA(back->next, T_PAIR)->a);
  TEST_ASSERT_EQUAL_UINT16(600, ERR_DATA(back->next, T_PAIR)->b);
  TEST_ASSERT_EQUAL_HEX32(T_PLAIN, back->next->next->tag);
  TEST_ASSERT_NULL(back->next->next->next);
  err_free(e);
  err_free(back);
  ASSERT_POOL_FULL();
}

static void test_record_parse_cut_short(void) {
  err_h e = ERR_NEW(T_PAIR, .a = 1, .b = 2);
  uint8_t buf[64];
  uint32_t n = err_record(e, buf, sizeof(buf));
  for (uint32_t len = 0; len < n; len++) {  /* every shorter prefix is refused; none crashes or leaks */
    err_h back = NULL;
    err_h rc = err_record_parse(buf, len, &back);
    TEST_ASSERT_NOT_NULL_MESSAGE(rc, "a record cut short was accepted");
    TEST_ASSERT_EQUAL_HEX32(ERR_RECORD_CUT, rc->tag);
    err_free(rc);
    err_free(back);
  }
  err_free(e);
  ASSERT_POOL_FULL();
}

static void test_log_lines(void) {
  err_h e = ERR_WRAP(ERR_NEW(T_PLAIN), T_PAIR, .a = 0x0A, .b = 0);
  err_log(e);
  TEST_ASSERT_EQUAL_INT(2, host_log_count());
  TEST_ASSERT_NOT_NULL(strstr(host_log_line(0), "T_PAIR"));
  TEST_ASSERT_NOT_NULL(strstr(host_log_line(0), "TEST.2"));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(host_log_line(0), "data: 0a"), host_log_line(0));
  TEST_ASSERT_NOT_NULL(strstr(host_log_line(1), "T_PLAIN"));
  TEST_ASSERT_EQUAL_INT(ESP_LOG_ERROR, host_log_level(0));  /* the chain's level: its highest sev */
  TEST_ASSERT_EQUAL_INT(ESP_LOG_ERROR, host_log_level(1));
  host_log_clear();
  err_log_enable(false);
  err_log(e);
  TEST_ASSERT_EQUAL_INT(0, host_log_count());
  err_log_enable(true);
  err_free(e);
}

static int s_sink_calls;
static uint32_t s_sink_tag;
static void sink(err_h chain, const struct cmd_req* cmd) {
  (void)cmd;
  s_sink_calls++;
  s_sink_tag = chain->tag;
}
static void test_report_sinks_and_frees(void) {
  s_sink_calls = 0;
  err_set_sink(sink);
  err_report(ERR_NEW(T_CRIT, .x = 1));
  TEST_ASSERT_EQUAL_INT(1, s_sink_calls);
  TEST_ASSERT_EQUAL_HEX32(T_CRIT, s_sink_tag);
  TEST_ASSERT_EQUAL_INT(1, host_log_count());
  ASSERT_POOL_FULL();  /* the reporter owned it and freed it */
  err_report(NULL);
  TEST_ASSERT_EQUAL_INT(1, s_sink_calls);
  err_set_sink(NULL);
}

/* ---- the pool ------------------------------------------------------------------------------------------------- */

static void test_pool_blocks_are_distinct_and_reusable(void) {
  err_h blk[CONFIG_ERRORS_POOL_BLOCKS];
  for (int round = 0; round < 2; round++) {
    for (int i = 0; i < CONFIG_ERRORS_POOL_BLOCKS; i++) {
      blk[i] = err_pool_alloc();
      TEST_ASSERT_NOT_NULL(blk[i]);
      TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)((uintptr_t)blk[i] % 4));
      for (int j = 0; j < i; j++) TEST_ASSERT_NOT_EQUAL(blk[j], blk[i]);  /* no block handed out twice */
    }
    TEST_ASSERT_NULL(err_pool_alloc());                                    /* empty */
    for (int i = CONFIG_ERRORS_POOL_BLOCKS - 1; i >= 0; i--) err_pool_free(blk[i]);
  }
  err_pool_free(NULL);  /* ignored */
  ASSERT_POOL_FULL();
}

static void test_node_payload_rest_is_zeroed(void) {
  /* A reused block must not carry the last owner's bytes: a record may declare a payload shorter than its tag's. */
  err_h dirty = ERR_NEW(T_PAIR, .a = 0xFF, .b = 0xFFFF);
  err_free(dirty);  /* its block is the next one handed out */
  uint8_t rec[1 + ERR_RECORD_STAMP_BYTES + 1 + 5 + 1] = {ERR_RECORD_SRC_INTERNAL};
  uint8_t* p = rec + 1 + ERR_RECORD_STAMP_BYTES;
  *p++ = 1;  /* one node */
  uint32_t tag = T_PAIR;
  memcpy(p, &tag, 4);
  p += 4;
  *p++ = 1;  /* payload of 1 byte (T_PAIR has 3) */
  *p++ = 0x07;
  err_h chain = NULL;
  TEST_ASSERT_NULL(err_record_parse(rec, (uint32_t)(p - rec), &chain));
  TEST_ASSERT_NOT_NULL(chain);
  TEST_ASSERT_EQUAL_UINT8(0x07, ERR_DATA(chain, T_PAIR)->a);
  TEST_ASSERT_EQUAL_HEX16(0, ERR_DATA(chain, T_PAIR)->b);  /* not 0xFFFF */
  err_free(chain);
  ASSERT_POOL_FULL();
}

/* ---- the tag registry ----------------------------------------------------------------------------------------- */

static void test_registry_lookup(void) {
  const err_tag_info_t* pair = err_tag_info(T_PAIR);
  TEST_ASSERT_NOT_NULL(pair);
  TEST_ASSERT_EQUAL_STRING("T_PAIR", pair->name);
  TEST_ASSERT_EQUAL_UINT8(3, pair->size);  /* packed: 1 + 2, no padding */
  TEST_ASSERT_EQUAL_UINT8(ERR_SEV_HIGH, pair->sev);
  TEST_ASSERT_EQUAL_STRING("TEST", err_module(MODULE_ID_TEST)->name);
  TEST_ASSERT_EQUAL_STRING("CORE", err_module(MODULE_ID_CORE)->name);
  TEST_ASSERT_EQUAL_UINT8(sizeof(esp_err_t), err_tag_info(ERR_ESP_ERR)->size);
  TEST_ASSERT_NULL(err_tag_info(ERR_TAG(MODULE_ID_TEST, 99)));  /* known module, unknown code */
  TEST_ASSERT_NULL(err_tag_info(ERR_TAG(901, 1)));     /* unknown module */
  TEST_ASSERT_NULL(err_module(901));
  TEST_ASSERT_EQUAL_UINT32(MODULE_ID_TEST, ERR_MODULE(T_PAIR));
  TEST_ASSERT_EQUAL_UINT16(2, ERR_CODE(T_PAIR));
}

/* ---- a tag this board does not know (a record from another board, a newer firmware) ---------------------------- */

#define T_UNKNOWN ERR_TAG(901, 5)

static void test_unknown_tag_is_kept_logged_and_has_no_severity(void) {
  uint8_t rec[1 + ERR_RECORD_STAMP_BYTES + 1 + 5 + 3] = {ERR_RECORD_SRC_INTERNAL};
  uint8_t* p = rec + 1 + ERR_RECORD_STAMP_BYTES;
  *p++ = 1;
  uint32_t tag = T_UNKNOWN;
  memcpy(p, &tag, 4);
  p += 4;
  *p++ = 3;
  *p++ = 0xA1;
  *p++ = 0xA2;
  *p++ = 0xA3;
  err_h chain = NULL;
  TEST_ASSERT_NULL(err_record_parse(rec, (uint32_t)(p - rec), &chain));
  TEST_ASSERT_ERR_TAG(T_UNKNOWN, chain);
  const uint8_t kept[] = {0xA1, 0xA2, 0xA3};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(kept, chain->data, 3);  /* payload kept as is */
  TEST_ASSERT_EQUAL_UINT8(ERR_SEV_NONE, err_sev(chain));
  err_log(chain);
  TEST_ASSERT_EQUAL_INT(1, host_log_count());
  TEST_ASSERT_NOT_NULL(strstr(host_log_line(0), "unknown tag 0x03850005"));
  uint8_t out[64];
  uint32_t n = err_record(chain, out, sizeof(out));  /* sent on without a payload: its size is unknown here */
  TEST_ASSERT_EQUAL_UINT32(1 + ERR_RECORD_STAMP_BYTES + 1 + 5, n);
  TEST_ASSERT_EQUAL_UINT8(0, out[n - 1]);
  err_free(chain);
  ASSERT_POOL_FULL();
}

/* ---- the record: limits --------------------------------------------------------------------------------------- */

static void test_record_cmd_request_cut_further_by_capacity(void) {
  uint8_t req[100] = {0};
  err_h e = ERR_NEW(T_PLAIN);
  uint8_t buf[64];
  const uint32_t cap = 14 + 10 + 1;  /* the header, 10 bytes of request room, the chain count */
  uint32_t n = err_record_cmd(1, 1, 0, req, sizeof(req), e, buf, cap);
  TEST_ASSERT_EQUAL_UINT32(cap, n);                          /* never past cap */
  TEST_ASSERT_EQUAL_HEX8(10 | ERR_RECORD_REQ_CUT, buf[13]);  /* request cut, and marked */
  TEST_ASSERT_EQUAL_HEX8(0x80, buf[24]);                     /* no node fits: 0 nodes, cut flag */
  TEST_ASSERT_EQUAL_UINT32(0, err_record_cmd(1, 1, 0, req, 4, e, buf, 14));  /* no room for the count */
  err_free(e);
}

static void test_record_parse_skips_command_header_and_request(void) {
  uint8_t req[5] = {1, 2, 3, 4, 5};
  err_h e = ERR_NEW(T_PAIR, .a = 9, .b = 10);
  uint8_t buf[64];
  uint32_t n = err_record_cmd(0x20, 7, 1, req, sizeof(req), e, buf, sizeof(buf));
  err_h back = NULL;
  TEST_ASSERT_NULL(err_record_parse(buf, n, &back));
  TEST_ASSERT_ERR_TAG(T_PAIR, back);
  TEST_ASSERT_EQUAL_UINT8(9, ERR_DATA(back, T_PAIR)->a);
  TEST_ASSERT_NULL(back->next);
  err_free(back);
  err_free(e);
  e = ERR_NEW(T_PLAIN);  /* a board's record too */
  n = err_record_board(3, e, buf, sizeof(buf));
  TEST_ASSERT_NULL(err_record_parse(buf, n, &back));
  TEST_ASSERT_ERR_TAG(T_PLAIN, back);
  err_free(back);
  err_free(e);
  ASSERT_POOL_FULL();
}

static void test_record_parse_with_a_short_pool_keeps_the_root_cause(void) {
  err_h e = ERR_WRAP(ERR_WRAP(ERR_NEW(T_PLAIN), T_PAIR, .a = 1), T_CRIT, .x = 2);
  uint8_t buf[64];
  uint32_t n = err_record(e, buf, sizeof(buf));
  err_free(e);
  err_h held[CONFIG_ERRORS_POOL_BLOCKS];
  int taken = 0;
  while (taken < CONFIG_ERRORS_POOL_BLOCKS - 2) held[taken++] = err_pool_alloc();  /* leave 2 blocks */
  err_h back = NULL;
  TEST_ASSERT_NULL(err_record_parse(buf, n, &back));
  TEST_ASSERT_ERR_TAG(T_PAIR, back);         /* the outermost node (T_CRIT) is the one left out */
  TEST_ASSERT_EQUAL_HEX32(T_PLAIN, back->next->tag);  /* the root cause is kept */
  TEST_ASSERT_NULL(back->next->next);
  err_free(back);
  while (taken > 0) err_pool_free(held[--taken]);
  ASSERT_POOL_FULL();
}

static void test_record_parse_survives_garbage(void) {
  /* What comes from the link may be anything: no crash, no leak, whatever the bytes are. */
  uint32_t x = 12345;
  for (int round = 0; round < 3000; round++) {
    uint8_t buf[80];
    uint32_t len = (x >> 8) % sizeof(buf);
    for (uint32_t i = 0; i < sizeof(buf); i++) {
      x = x * 1664525u + 1013904223u;
      buf[i] = (uint8_t)(x >> 24);
    }
    buf[0] %= 4;  /* mostly a valid source byte */
    err_h chain = NULL;
    err_h rc = err_record_parse(buf, len, &chain);
    err_free(rc);
    err_free(chain);
  }
  ASSERT_POOL_FULL();
}

/* ---- guards against a corrupted (cyclic) chain ---------------------------------------------------------------- */

static void test_cyclic_chain_is_bounded(void) {
  err_h a = ERR_NEW(T_PLAIN);
  err_h b = ERR_WRAP(NULL, T_CRIT, .x = 1);
  a->next = b;
  b->next = a;                                                  /* a -> b -> a -> ... */
  TEST_ASSERT_EQUAL_UINT8(ERR_SEV_CRITICAL, err_chain_sev(a));  /* these return */
  (void)err_root_tag(a);
  TEST_ASSERT_NULL(err_find(a, T_MED));                         /* no node has it: the walk still ends */
  uint8_t buf[256];
  uint32_t n = err_record(a, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0 && n <= sizeof(buf));
  TEST_ASSERT_TRUE((buf[9] & 0x7F) <= CONFIG_ERRORS_POOL_BLOCKS + 1);
  err_log(a);
  TEST_ASSERT_NOT_NULL(strstr(host_log_line(host_log_count() - 1), "corrupted"));
  b->next = NULL;  /* break the cycle: now it can be freed normally */
  err_free(a);
  ASSERT_POOL_FULL();
}

/* ---- the log -------------------------------------------------------------------------------------------------- */

static void test_log_level_follows_chain_severity(void) {
  err_h low = ERR_NEW(T_PLAIN);
  err_h med = ERR_WRAP(ERR_NEW(T_PLAIN), T_MED);
  err_h crit = ERR_WRAP(ERR_NEW(T_PLAIN), T_CRIT, .x = 0);
  err_log(low);
  TEST_ASSERT_EQUAL_INT(ESP_LOG_INFO, host_log_level(0));
  host_log_clear();
  err_log(med);
  TEST_ASSERT_EQUAL_INT(ESP_LOG_WARN, host_log_level(0));
  host_log_clear();
  err_log(crit);
  TEST_ASSERT_EQUAL_INT(ESP_LOG_ERROR, host_log_level(0));
  err_log(NULL);
  err_free(low);
  err_free(med);
  err_free(crit);
}

/* ---- reporting ------------------------------------------------------------------------------------------------ */

static const struct cmd_req* s_sink_cmd;
static void sink_cmd(err_h chain, const struct cmd_req* cmd) {
  (void)chain;
  s_sink_cmd = cmd;
}
static void test_report_ex_logs_context_first_and_passes_the_command(void) {
  const struct cmd_req* cmd = (const struct cmd_req*)0x1234;  /* never read by errors */
  s_sink_cmd = NULL;
  err_set_sink(sink_cmd);
  err_report_ex(ERR_NEW(T_PAIR, .a = 1, .b = 2), "while running X", cmd);
  TEST_ASSERT_EQUAL_PTR(cmd, s_sink_cmd);
  TEST_ASSERT_EQUAL_INT(2, host_log_count());
  TEST_ASSERT_EQUAL_STRING("while running X", host_log_line(0));
  TEST_ASSERT_NOT_NULL(strstr(host_log_line(1), "T_PAIR"));
  err_set_sink(NULL);
  ASSERT_POOL_FULL();
}

static void test_report_with_log_off_still_sinks_and_frees(void) {
  s_sink_calls = 0;
  err_set_sink(sink);
  err_log_enable(false);
  err_report(ERR_NEW(T_CRIT, .x = 1));
  err_log_enable(true);
  TEST_ASSERT_EQUAL_INT(0, host_log_count());
  TEST_ASSERT_EQUAL_INT(1, s_sink_calls);
  err_set_sink(NULL);
  ASSERT_POOL_FULL();
}

static uint8_t s_board_seen = 0xFF;
static uint32_t s_board_tag;
static void board_sink(err_h chain, uint8_t board) {
  s_board_seen = board;
  s_board_tag = chain->tag;
}
static void test_report_board(void) {
  s_board_seen = 0xFF;
  err_set_board_sink(board_sink);
  err_report_board(ERR_NEW(T_MED), 5);
  TEST_ASSERT_EQUAL_UINT8(5, s_board_seen);
  TEST_ASSERT_EQUAL_HEX32(T_MED, s_board_tag);
  TEST_ASSERT_EQUAL_STRING("board", host_log_line(0));
  err_report_board(NULL, 6);
  TEST_ASSERT_EQUAL_UINT8(5, s_board_seen);  /* NULL: nothing reported */
  err_set_board_sink(NULL);
  ASSERT_POOL_FULL();
}

/* ---- the macros ----------------------------------------------------------------------------------------------- */

static err_h in_range(uint32_t v) {
  ERR_CHECK_IN_RANGE(v, 0, 10, T_PLAIN);
  return NULL;
}
static void test_check_in_range_is_inclusive(void) {
  TEST_ASSERT_NULL(in_range(0));
  TEST_ASSERT_NULL(in_range(10));
  err_h e = in_range(11);
  TEST_ASSERT_NOT_NULL(e);
  err_free(e);
  TEST_ASSERT_TRUE(err_in_range(-1, -1, 1));
  TEST_ASSERT_FALSE(err_in_range(2, -1, 1));
}

static int s_range_calls;
static int16_t next_i16(void) {
  s_range_calls++;
  return -5;
}
static err_h range_i(int16_t v) {
  ERR_CHECK_RANGE(v, -3, 7);
  return NULL;
}
static err_h range_u(uint32_t v) {
  ERR_CHECK_RANGE(v, 0, 10);
  return NULL;
}
static err_h range_once(void) {
  ERR_CHECK_RANGE(next_i16(), -3, 7);
  return NULL;
}
static err_h range_f(float v) {
  ERR_CHECK_RANGE(v, -1.5f, 2.5f);
  return NULL;
}
static err_h range_d(double v) {
  ERR_CHECK_RANGE(v, 0, 1);
  return NULL;
}
static void test_check_range_picks_the_tag_by_type(void) {
  TEST_ASSERT_NULL(range_i(-3));
  TEST_ASSERT_NULL(range_i(7));
  err_h e = range_i(-4);
  TEST_ASSERT_EQUAL_HEX32(ERR_RANGE, e->tag);
  TEST_ASSERT_EQUAL_INT32(-4, ERR_DATA(e, ERR_RANGE)->value);
  TEST_ASSERT_EQUAL_INT32(-3, ERR_DATA(e, ERR_RANGE)->min);
  TEST_ASSERT_EQUAL_INT32(7, ERR_DATA(e, ERR_RANGE)->max);
  err_free(e);

  e = range_u(4000000000u);  // the check is exact; the payload is int32_t
  TEST_ASSERT_EQUAL_HEX32(ERR_RANGE, e->tag);
  err_free(e);

  e = range_once();
  TEST_ASSERT_EQUAL_INT(1, s_range_calls);  // v evaluated once
  TEST_ASSERT_EQUAL_INT32(-5, ERR_DATA(e, ERR_RANGE)->value);
  err_free(e);

  TEST_ASSERT_NULL(range_f(-1.5f));
  TEST_ASSERT_NULL(range_f(2.5f));
  e = range_f(2.75f);
  TEST_ASSERT_EQUAL_HEX32(ERR_RANGE_F, e->tag);
  TEST_ASSERT_EQUAL_FLOAT(2.75f, ERR_DATA(e, ERR_RANGE_F)->value);
  TEST_ASSERT_EQUAL_FLOAT(-1.5f, ERR_DATA(e, ERR_RANGE_F)->min);
  TEST_ASSERT_EQUAL_FLOAT(2.5f, ERR_DATA(e, ERR_RANGE_F)->max);
  err_free(e);

  e = range_f(NAN);  // never in range
  TEST_ASSERT_EQUAL_HEX32(ERR_RANGE_F, e->tag);
  err_free(e);

  e = range_d(1.25);
  TEST_ASSERT_EQUAL_HEX32(ERR_RANGE_F, e->tag);
  TEST_ASSERT_EQUAL_FLOAT(1.25f, ERR_DATA(e, ERR_RANGE_F)->value);
  err_free(e);
  ASSERT_POOL_FULL();
}

/* ---- the pool under real concurrency -------------------------------------------------------------------------- */

typedef struct {
  uint8_t id;
  int bad;  /* nodes that did not hold what their owner wrote: a block handed to two tasks at once */
} worker_t;

static void* pool_worker(void* arg) {
  worker_t* w = arg;
  for (int i = 0; i < 20000; i++) {
    err_h held[4];
    for (int k = 0; k < 4; k++) {
      held[k] = ERR_NEW(T_PAIR, .a = w->id, .b = (uint16_t)(i + k));
    }
    for (int k = 0; k < 4; k++) {
      if (held[k]->tag != T_PAIR || ERR_DATA(held[k], T_PAIR)->a != w->id ||
          ERR_DATA(held[k], T_PAIR)->b != (uint16_t)(i + k)) {
        w->bad++;
      }
      err_free(held[k]);
    }
  }
  return NULL;
}

static void test_pool_is_safe_across_tasks(void) {
  enum { TASKS = 4 };  /* 4 tasks x 4 nodes held = 16 of the pool's 32: it never runs dry here */
  pthread_t th[TASKS];
  worker_t w[TASKS];
  for (int i = 0; i < TASKS; i++) {
    w[i] = (worker_t){.id = (uint8_t)(i + 1), .bad = 0};
    pthread_create(&th[i], NULL, pool_worker, &w[i]);
  }
  for (int i = 0; i < TASKS; i++) {
    pthread_join(th[i], NULL);
    TEST_ASSERT_EQUAL_INT(0, w[i].bad);
  }
  ASSERT_POOL_FULL();  /* and not a block lost or twice on the free list */
}

/* ---- nodes: limits and edges ---------------------------------------------------------------------------------- */

static void test_a_payload_of_exactly_a_node_is_kept_whole(void) {
  uint8_t data[ERR_DATA_MAX];
  for (uint32_t i = 0; i < ERR_DATA_MAX; i++) data[i] = (uint8_t)(0xA0 + i);
  err_h e = ERR__NEW(T_PLAIN, data, ERR_DATA_MAX, NULL);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(data, err_data(e), ERR_DATA_MAX);
  err_h none = ERR__NEW(T_PLAIN, NULL, 0, e);  /* no payload: data may be NULL */
  TEST_ASSERT_EQUAL_PTR(e, err_cause(none));
  const uint8_t zeros[ERR_DATA_MAX] = {0};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(zeros, err_data(none), ERR_DATA_MAX);  /* the whole payload zeroed */
  err_free(none);
  ASSERT_POOL_FULL();
}

static void test_the_queries_on_one_node_and_on_repeats(void) {
  err_h one = ERR_NEW(T_MED);
  TEST_ASSERT_EQUAL_HEX32(T_MED, err_root_tag(one));  /* a single node is its own root */
  TEST_ASSERT_EQUAL_UINT8(ERR_SEV_MEDIUM, err_chain_sev(one));
  TEST_ASSERT_EQUAL_UINT8(ERR_SEV_NONE, err_sev(NULL));
  TEST_ASSERT_TRUE(err_drop(&one, T_MED));            /* the whole chain is the cause */
  TEST_ASSERT_NULL(one);
  err_h twice = ERR_WRAP(ERR_NEW(T_PAIR, .a = 2), T_PAIR, .a = 1);
  TEST_ASSERT_EQUAL_PTR(twice, err_find(twice, T_PAIR));  /* the outermost of two */
  TEST_ASSERT_EQUAL_UINT8(1, ERR_DATA(err_find(twice, T_PAIR), T_PAIR)->a);
  err_free(twice);
  ASSERT_POOL_FULL();
}

static void test_the_pool_empty_node_as_a_cause_is_never_freed(void) {
  err_h held[CONFIG_ERRORS_POOL_BLOCKS];
  for (int i = 0; i < CONFIG_ERRORS_POOL_BLOCKS; i++) held[i] = ERR_NEW(T_PLAIN);
  err_h empty = ERR_NEW(T_PLAIN);
  TEST_ASSERT_EQUAL_HEX32(ERR_POOL_EMPTY, err_tag(empty));
  TEST_ASSERT_EQUAL_PTR(empty, ERR_NEW(T_MED));            /* always the same static node */
  err_free(held[0]);                                       /* one block back */
  err_h outer = ERR_WRAP(empty, T_PAIR, .a = 1);           /* a real node over the static one */
  TEST_ASSERT_EQUAL_HEX32(T_PAIR, err_tag(outer));
  TEST_ASSERT_EQUAL_PTR(empty, err_cause(outer));
  TEST_ASSERT_EQUAL_UINT8(ERR_SEV_HIGH, err_chain_sev(outer));  /* T_PAIR over the LOW static node */
  err_free(outer);                                         /* frees the node, skips the static cause */
  TEST_ASSERT_EQUAL_HEX32(ERR_POOL_EMPTY, err_tag(empty)); /* still whole */
  TEST_ASSERT_NULL(err_cause(empty));
  for (int i = 1; i < CONFIG_ERRORS_POOL_BLOCKS; i++) err_free(held[i]);
  ASSERT_POOL_FULL();
}

/* ---- the macros: the rest ------------------------------------------------------------------------------------- */

static err_h esp_step(esp_err_t rc) {
  ERR_TRY_ESP(rc);
  return NULL;
}
static void test_try_esp_and_keep_first_after_a_success(void) {
  TEST_ASSERT_NULL(esp_step(ESP_OK));
  err_h e = esp_step(ESP_ERR_NO_MEM);
  TEST_ASSERT_ERR_TAG(ERR_ESP_ERR, e);
  TEST_ASSERT_EQUAL_INT(ESP_ERR_NO_MEM, ERR_DATA(e, ERR_ESP_ERR)->code);
  err_free(e);
  err_h acc = NULL;
  ERR_KEEP_FIRST(acc, NULL);                 /* a step that succeeded keeps nothing */
  TEST_ASSERT_NULL(acc);
  ERR_KEEP_FIRST(acc, ERR_NEW(T_MED));       /* the first failure is kept */
  ERR_KEEP_FIRST(acc, NULL);
  TEST_ASSERT_ERR_TAG(T_MED, acc);
  err_free(acc);
  ASSERT_POOL_FULL();
}

/* ---- the log: the rest ---------------------------------------------------------------------------------------- */

static void test_log_names_an_esp_code_and_the_call_site(void) {
  err_h e = ERR_ESP(ESP_ERR_NO_MEM);
  err_log(e);
  TEST_ASSERT_EQUAL_INT(1, host_log_count());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(host_log_line(0), "ESP_ERR_NO_MEM (0x101)"), host_log_line(0));
#if CONFIG_ERRORS_INFO
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(host_log_line(0), "@ test_errors.c:"), host_log_line(0));
  TEST_ASSERT_NOT_NULL(strstr(host_log_line(0), "test_log_names_an_esp_code_and_the_call_site()"));
#else
  TEST_ASSERT_NULL_MESSAGE(strstr(host_log_line(0), " @ "), host_log_line(0));  /* no call site at all */
#endif
  err_free(e);
}

static void test_log_ctx_prints_nothing_for_no_chain(void) {
  err_log_ctx(NULL, "never printed");
  TEST_ASSERT_EQUAL_INT(0, host_log_count());
  err_h e = ERR_NEW(T_PLAIN);
  err_log_ctx(e, NULL);  /* NULL context: err_log */
  TEST_ASSERT_EQUAL_INT(1, host_log_count());
  err_free(e);
}

/* ---- the record: bad arguments -------------------------------------------------------------------------------- */

static void test_record_parse_refuses_null(void) {
  uint8_t rec[16] = {0};
  err_h chain = (err_h)0x1;
  err_h rc = err_record_parse(rec, sizeof(rec), NULL);  /* nowhere to put the chain */
  TEST_ASSERT_ERR_TAG(ERR_NULL_PTR, rc);
  err_free(rc);
  rc = err_record_parse(NULL, 4, &chain);                /* bytes promised, none given */
  TEST_ASSERT_ERR_TAG(ERR_NULL_PTR, rc);
  err_free(rc);
  ASSERT_POOL_FULL();
}

/* ---- with asserts off (the variant no_assert): a misuse is ignored and nothing breaks ------------------------- */

#ifdef NDEBUG
static void test_double_free_and_a_foreign_pointer_leave_the_pool_whole(void) {
  err_h a = err_pool_alloc();
  err_pool_free(a);
  err_pool_free(a);                          /* a second time: ignored, not on the free list twice */
  uint32_t local[16];
  err_pool_free((err_h)(void*)local);        /* not a pool block */
  err_h b = err_pool_alloc();
  err_pool_free((err_h)(void*)((uint8_t*)(void*)b + 4));  /* inside a block, not its start */
  err_pool_free(b);
  /* Every block once and none twice: the whole pool hands out distinct blocks, then runs dry. */
  err_h all[CONFIG_ERRORS_POOL_BLOCKS];
  for (int i = 0; i < CONFIG_ERRORS_POOL_BLOCKS; i++) {
    all[i] = err_pool_alloc();
    TEST_ASSERT_NOT_NULL(all[i]);
    for (int j = 0; j < i; j++) TEST_ASSERT_NOT_EQUAL(all[j], all[i]);
  }
  TEST_ASSERT_NULL(err_pool_alloc());
  for (int i = 0; i < CONFIG_ERRORS_POOL_BLOCKS; i++) err_pool_free(all[i]);
}

static void test_a_payload_longer_than_a_node_is_cut(void) {
  /* Every other block is held with a known payload: a write past the node would land in one of them. */
  err_h held[CONFIG_ERRORS_POOL_BLOCKS - 1];
  for (int i = 0; i < CONFIG_ERRORS_POOL_BLOCKS - 1; i++) held[i] = ERR_NEW(T_PAIR, .a = 0x55, .b = 0x5555);
  uint8_t data[ERR_DATA_MAX + 8];
  for (uint32_t i = 0; i < sizeof(data); i++) data[i] = (uint8_t)i;
  err_h e = ERR__NEW(T_PLAIN, data, sizeof(data), NULL);  /* the last block */
  TEST_ASSERT_EQUAL_HEX32(T_PLAIN, err_tag(e));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(data, err_data(e), ERR_DATA_MAX);
  for (int i = 0; i < CONFIG_ERRORS_POOL_BLOCKS - 1; i++) {
    TEST_ASSERT_EQUAL_HEX32(T_PAIR, err_tag(held[i]));
    TEST_ASSERT_NULL(err_cause(held[i]));
    TEST_ASSERT_EQUAL_UINT8(0x55, ERR_DATA(held[i], T_PAIR)->a);
    TEST_ASSERT_EQUAL_HEX16(0x5555, ERR_DATA(held[i], T_PAIR)->b);
    err_free(held[i]);
  }
  err_free(e);
  ASSERT_POOL_FULL();
}
#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_new_has_tag_and_data);
  RUN_TEST(test_wrap_chain_order_root_and_severity);
  RUN_TEST(test_accessors_and_find);
  RUN_TEST(test_drop_takes_away_only_the_cause_it_names);
  RUN_TEST(test_pool_exhaustion);
  RUN_TEST(test_try_check_esp);
  RUN_TEST(test_keep_first);
  RUN_TEST(test_record_internal_bytes);
  RUN_TEST(test_record_cut_by_capacity);
  RUN_TEST(test_record_cmd_cuts_request);
  RUN_TEST(test_record_board);
  RUN_TEST(test_record_round_trip);
  RUN_TEST(test_record_parse_cut_short);
  RUN_TEST(test_log_lines);
  RUN_TEST(test_report_sinks_and_frees);
  RUN_TEST(test_pool_blocks_are_distinct_and_reusable);
  RUN_TEST(test_node_payload_rest_is_zeroed);
  RUN_TEST(test_registry_lookup);
  RUN_TEST(test_unknown_tag_is_kept_logged_and_has_no_severity);
  RUN_TEST(test_record_cmd_request_cut_further_by_capacity);
  RUN_TEST(test_record_parse_skips_command_header_and_request);
  RUN_TEST(test_record_parse_with_a_short_pool_keeps_the_root_cause);
  RUN_TEST(test_record_parse_survives_garbage);
  RUN_TEST(test_cyclic_chain_is_bounded);
  RUN_TEST(test_log_level_follows_chain_severity);
  RUN_TEST(test_report_ex_logs_context_first_and_passes_the_command);
  RUN_TEST(test_report_with_log_off_still_sinks_and_frees);
  RUN_TEST(test_report_board);
  RUN_TEST(test_check_in_range_is_inclusive);
  RUN_TEST(test_check_range_picks_the_tag_by_type);
  RUN_TEST(test_pool_is_safe_across_tasks);
  RUN_TEST(test_a_payload_of_exactly_a_node_is_kept_whole);
  RUN_TEST(test_the_queries_on_one_node_and_on_repeats);
  RUN_TEST(test_the_pool_empty_node_as_a_cause_is_never_freed);
  RUN_TEST(test_try_esp_and_keep_first_after_a_success);
  RUN_TEST(test_log_names_an_esp_code_and_the_call_site);
  RUN_TEST(test_log_ctx_prints_nothing_for_no_chain);
  RUN_TEST(test_record_parse_refuses_null);
#ifdef NDEBUG
  RUN_TEST(test_double_free_and_a_foreign_pointer_leave_the_pool_whole);
  RUN_TEST(test_a_payload_longer_than_a_node_is_cut);
#endif
  return UNITY_END();
}
