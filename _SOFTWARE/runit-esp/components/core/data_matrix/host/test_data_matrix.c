/* Host tests of core/data_matrix (data_matrix.c + data_matrix_router.c). */
#include <string.h>

#include "data_matrix.h"
#include "data_matrix_id.h"
#include "data_matrix_router.h"
#include "host_assert.h"
#include "router.h"
#include "unity.h"

typedef struct {
  data_matrix_port_t port;
  int calls;
  data_matrix_port_id_t last_from;
  uint8_t last_flags;
  uint8_t last_data[32];
  uint32_t last_len;
} spy_port_t;

static void spy_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* data,
                   uint32_t len) {
  spy_port_t* s = DATA_MATRIX_OWNER(self, spy_port_t, port);
  s->calls++;
  s->last_from = from;
  s->last_flags = flags;
  s->last_len = len < sizeof(s->last_data) ? len : sizeof(s->last_data);
  if (s->last_len > 0) {
    memcpy(s->last_data, data, s->last_len);
  }
}

static spy_port_t make_spy(uint8_t kind, uint8_t id, bool with_in) {
  return (spy_port_t){.port = {.id = DATA_MATRIX_PORT(kind, id), .in = with_in ? spy_in : NULL}};
}

static err_h route_call(uint8_t dir, uint8_t op, const void* args, uint32_t args_len, uint8_t* reply,
                        uint32_t reply_cap, uint32_t* reply_len) {
  uint8_t req[64];
  req[0] = MODULE_ID_DATA_MATRIX;
  req[1] = dir;
  req[2] = op;
  if (args_len > 0) {
    memcpy(req + 3, args, args_len);
  }
  cmd_out_t out = {.data = reply, .cap = reply_cap, .len = 0};
  err_h e = router_route((cmd_in_t){req, 3u + args_len}, &out);
  if (reply_len != NULL) {
    *reply_len = out.len;
  }
  return e;
}

void setUp(void) {}
void tearDown(void) {}

static void test_register_unregister_and_table_limits(void) {
  err_h e = data_matrix_register(NULL);
  TEST_ASSERT_ERR_TAG(ERR_BAD_ARG, e);
  err_free(e);

  e = data_matrix_unregister(NULL);
  TEST_ASSERT_ERR_TAG(ERR_BAD_ARG, e);
  err_free(e);

  spy_port_t ports[CONFIG_DATA_MATRIX_PORTS + 1];
  for (int i = 0; i < CONFIG_DATA_MATRIX_PORTS; i++) {
    ports[i] = make_spy(DATA_MATRIX_PORT_PROVIDER, (uint8_t)(i + 1), true);
    TEST_ASSERT_NULL(data_matrix_register(&ports[i].port));
  }
  /* Duplicate ID refused */
  spy_port_t dup = make_spy(DATA_MATRIX_PORT_PROVIDER, 1, true);
  e = data_matrix_register(&dup.port);
  TEST_ASSERT_ERR_TAG(ERR_BAD_ARG, e);
  err_free(e);

  /* Full table refused */
  ports[CONFIG_DATA_MATRIX_PORTS] = make_spy(DATA_MATRIX_PORT_PROVIDER, 99, true);
  e = data_matrix_register(&ports[CONFIG_DATA_MATRIX_PORTS].port);
  TEST_ASSERT_ERR_TAG(ERR_FULL, e);
  err_free(e);

  /* Unregistering twice fails second time; frees slot */
  TEST_ASSERT_NULL(data_matrix_unregister(&ports[0].port));
  e = data_matrix_unregister(&ports[0].port);
  TEST_ASSERT_ERR_TAG(ERR_BAD_ARG, e);
  err_free(e);

  TEST_ASSERT_NULL(data_matrix_register(&ports[CONFIG_DATA_MATRIX_PORTS].port));
  TEST_ASSERT_NULL(data_matrix_unregister(&ports[CONFIG_DATA_MATRIX_PORTS].port));
  for (int i = 1; i < CONFIG_DATA_MATRIX_PORTS; i++) {
    TEST_ASSERT_NULL(data_matrix_unregister(&ports[i].port));
  }
}

static void test_connect_fanout_except_send_to_and_target_limit(void) {
  spy_port_t src = make_spy(DATA_MATRIX_PORT_MODULE, 1, false);  /* output only */
  spy_port_t dst[CONFIG_DATA_MATRIX_TARGETS + 1];
  TEST_ASSERT_NULL(data_matrix_register(&src.port));
  for (int i = 0; i < CONFIG_DATA_MATRIX_TARGETS + 1; i++) {
    dst[i] = make_spy(DATA_MATRIX_PORT_PROVIDER, (uint8_t)(10 + i), true);
    TEST_ASSERT_NULL(data_matrix_register(&dst[i].port));
  }

  /* Refusals: NULL, self-link, unknown port, target with in == NULL */
  err_h e = data_matrix_connect(NULL);
  TEST_ASSERT_ERR_TAG(ERR_BAD_ARG, e);
  err_free(e);
  e = data_matrix_connect(&(data_matrix_link_t){dst[0].port.id, dst[0].port.id});
  TEST_ASSERT_ERR_TAG(ERR_BAD_ARG, e);
  err_free(e);
  e = data_matrix_connect(&(data_matrix_link_t){src.port.id, DATA_MATRIX_PORT(DATA_MATRIX_PORT_PROVIDER, 250)});
  TEST_ASSERT_ERR_TAG(ERR_BAD_ARG, e);
  err_free(e);
  e = data_matrix_connect(&(data_matrix_link_t){dst[0].port.id, src.port.id});
  TEST_ASSERT_ERR_TAG(ERR_BAD_ARG, e);
  err_free(e);

  TEST_ASSERT_FALSE(data_matrix_linked(&src.port));
  for (int i = 0; i < CONFIG_DATA_MATRIX_TARGETS; i++) {
    TEST_ASSERT_NULL(data_matrix_connect(&(data_matrix_link_t){src.port.id, dst[i].port.id}));
  }
  /* Idempotent reconnect succeeds even when at target cap */
  TEST_ASSERT_NULL(data_matrix_connect(&(data_matrix_link_t){src.port.id, dst[0].port.id}));
  /* One more distinct target hits DATA_MATRIX_FULL */
  e = data_matrix_connect(&(data_matrix_link_t){src.port.id, dst[CONFIG_DATA_MATRIX_TARGETS].port.id});
  TEST_ASSERT_ERR_TAG(ERR_FULL, e);
  err_free(e);
  TEST_ASSERT_TRUE(data_matrix_linked(&src.port));

  /* Fan-out send */
  const uint8_t msg[] = {0xAA, 0xBB, 0xCC};
  data_matrix_send(&src.port, CMD_SEND_URGENT, msg, sizeof(msg));
  for (int i = 0; i < CONFIG_DATA_MATRIX_TARGETS; i++) {
    TEST_ASSERT_EQUAL_INT(1, dst[i].calls);
    TEST_ASSERT_EQUAL_HEX16(src.port.id, dst[i].last_from);
    TEST_ASSERT_EQUAL_HEX8(CMD_SEND_URGENT, dst[i].last_flags);
    TEST_ASSERT_EQUAL_UINT32(sizeof(msg), dst[i].last_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, dst[i].last_data, sizeof(msg));
  }
  TEST_ASSERT_EQUAL_INT(0, dst[CONFIG_DATA_MATRIX_TARGETS].calls);

  /* send_except skips the named target */
  data_matrix_send_except(&src.port, dst[1].port.id, CMD_SEND_NOWAIT, msg, 1);
  TEST_ASSERT_EQUAL_INT(2, dst[0].calls);
  TEST_ASSERT_EQUAL_INT(1, dst[1].calls);
  TEST_ASSERT_EQUAL_INT(2, dst[2].calls);

  /* send_to works without any link, and returns false on missing / no-input ports */
  TEST_ASSERT_TRUE(data_matrix_send_to(&src.port, dst[CONFIG_DATA_MATRIX_TARGETS].port.id, 0, msg, 2));
  TEST_ASSERT_EQUAL_INT(1, dst[CONFIG_DATA_MATRIX_TARGETS].calls);
  TEST_ASSERT_FALSE(data_matrix_send_to(&dst[0].port, src.port.id, 0, msg, 2));
  TEST_ASSERT_FALSE(data_matrix_send_to(&src.port, 0x7777u, 0, msg, 2));

  /* Unregistering a target drops links pointing to it */
  TEST_ASSERT_NULL(data_matrix_unregister(&dst[1].port));
  data_matrix_link_t links[8];
  TEST_ASSERT_EQUAL_UINT32(CONFIG_DATA_MATRIX_TARGETS - 1u, data_matrix_links(links, 8));

  TEST_ASSERT_NULL(data_matrix_unregister(&src.port));
  for (int i = 0; i < CONFIG_DATA_MATRIX_TARGETS + 1; i++) {
    if (i != 1) {
      TEST_ASSERT_NULL(data_matrix_unregister(&dst[i].port));
    }
  }
}

static void test_router_connect_disconnect_and_list(void) {
  spy_port_t a = make_spy(DATA_MATRIX_PORT_MODULE, 0x10, true);
  spy_port_t b = make_spy(DATA_MATRIX_PORT_PROVIDER, 0x05, true);
  TEST_ASSERT_NULL(data_matrix_register(&a.port));
  TEST_ASSERT_NULL(data_matrix_register(&b.port));

  data_matrix_link_t link = {.from = a.port.id, .to = b.port.id};
  uint8_t reply[64];
  uint32_t rlen = 0;

  TEST_ASSERT_NULL(route_call(CMD_DIR_WRITE, DATA_MATRIX_OP_CONNECT, &link, sizeof(link), reply, sizeof(reply), &rlen));
  TEST_ASSERT_NULL(route_call(CMD_DIR_READ, DATA_MATRIX_OP_LIST, NULL, 0, reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_UINT32(1u + sizeof(data_matrix_link_t), rlen);
  TEST_ASSERT_EQUAL_UINT8(1, reply[0]);
  data_matrix_link_t got;
  memcpy(&got, reply + 1, sizeof(got));
  TEST_ASSERT_EQUAL_HEX16(a.port.id, got.from);
  TEST_ASSERT_EQUAL_HEX16(b.port.id, got.to);

  /* Disconnect twice is idempotent */
  TEST_ASSERT_NULL(route_call(CMD_DIR_WRITE, DATA_MATRIX_OP_DISCONNECT, &link, sizeof(link), reply, sizeof(reply), &rlen));
  TEST_ASSERT_NULL(route_call(CMD_DIR_WRITE, DATA_MATRIX_OP_DISCONNECT, &link, sizeof(link), reply, sizeof(reply), &rlen));
  TEST_ASSERT_NULL(route_call(CMD_DIR_READ, DATA_MATRIX_OP_LIST, NULL, 0, reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_UINT8(0, reply[0]);

  TEST_ASSERT_NULL(data_matrix_unregister(&a.port));
  TEST_ASSERT_NULL(data_matrix_unregister(&b.port));
}

int main(void) {
  TEST_ASSERT_NULL(router_root_init());
  UNITY_BEGIN();
  RUN_TEST(test_register_unregister_and_table_limits);
  RUN_TEST(test_connect_fanout_except_send_to_and_target_limit);
  RUN_TEST(test_router_connect_disconnect_and_list);
  return UNITY_END();
}
