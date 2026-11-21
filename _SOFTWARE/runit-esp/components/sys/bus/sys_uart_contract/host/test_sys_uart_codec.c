/* Host tests of sys_uart_codec (COBS + CRC16-CCITT framing & urgent/normal TX queue). */
#include <string.h>

#include "sys_uart_codec.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void roundtrip_framed(const uint8_t* payload, uint32_t len) {
  sys_uart_txq_t q;
  TEST_ASSERT_TRUE(sys_uart_txq_init(&q, 2048, true));
  TEST_ASSERT_TRUE(sys_uart_txq_push(&q, (cmd_in_t){payload, len}, CMD_SEND_NOWAIT, 0));

  const uint32_t wire_len = sys_uart_txq_pop(&q, 0);
  TEST_ASSERT_GREATER_THAN_UINT32(3, wire_len);
  TEST_ASSERT_EQUAL_UINT8(0x00, q.line[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00, q.line[wire_len - 1u]);
  /* No 0x00 delimiters inside COBS body */
  for (uint32_t i = 1; i < wire_len - 1u; i++) {
    TEST_ASSERT_NOT_EQUAL_UINT8(0x00, q.line[i]);
  }

  uint8_t work[sizeof(q.line)];
  memcpy(work, q.line + 1, wire_len - 2u);
  const uint32_t dec_len = sys_uart_unframe(work, wire_len - 2u);
  TEST_ASSERT_EQUAL_UINT32(len, dec_len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, work, len);

  sys_uart_txq_free(&q);
}

static void test_cobs_and_crc_roundtrip_edge_cases(void) {
  /* 1 byte zero, all zeros, all 0xFF (tests 254-byte COBS block boundaries), max frame 512 B */
  const uint8_t single_zero[] = {0x00};
  roundtrip_framed(single_zero, sizeof(single_zero));

  uint8_t all_zeros[32] = {0};
  roundtrip_framed(all_zeros, sizeof(all_zeros));

  uint8_t no_zeros_254[254];
  memset(no_zeros_254, 0xA5, sizeof(no_zeros_254));
  roundtrip_framed(no_zeros_254, sizeof(no_zeros_254));

  uint8_t no_zeros_255[255];
  memset(no_zeros_255, 0x5A, sizeof(no_zeros_255));
  roundtrip_framed(no_zeros_255, sizeof(no_zeros_255));

  uint8_t max_frame[CMD_FRAME_MAX];
  for (uint32_t i = 0; i < CMD_FRAME_MAX; i++) {
    max_frame[i] = (uint8_t)((i % 17 == 0) ? 0 : (i * 37u + 1u));
  }
  roundtrip_framed(max_frame, sizeof(max_frame));
}

static void test_unframe_rejects_corrupt_crc_bad_cobs_and_oversize(void) {
  sys_uart_txq_t q;
  TEST_ASSERT_TRUE(sys_uart_txq_init(&q, 2048, true));
  const uint8_t msg[] = {0x01, 0x02, 0x00, 0x03, 0x04};
  TEST_ASSERT_TRUE(sys_uart_txq_push(&q, (cmd_in_t){msg, sizeof(msg)}, CMD_SEND_NOWAIT, 0));
  const uint32_t wire_len = sys_uart_txq_pop(&q, 0);
  const uint32_t body_len = wire_len - 2u;

  /* Flip a payload bit -> CRC mismatch */
  uint8_t bad_crc[64];
  memcpy(bad_crc, q.line + 1, body_len);
  bad_crc[1] ^= 0x01u;
  TEST_ASSERT_EQUAL_UINT32(0, sys_uart_unframe(bad_crc, body_len));

  /* Truncated frame -> rejected */
  uint8_t cut[64];
  memcpy(cut, q.line + 1, body_len);
  TEST_ASSERT_EQUAL_UINT32(0, sys_uart_unframe(cut, body_len - 1u));

  /* Embedded zero -> invalid COBS */
  uint8_t z[4] = {0x02, 0x00, 0x01, 0x01};
  TEST_ASSERT_EQUAL_UINT32(0, sys_uart_unframe(z, sizeof(z)));

  /* Empty or too short for CRC */
  uint8_t tiny[3] = {0x03, 0x11, 0x22};
  TEST_ASSERT_EQUAL_UINT32(0, sys_uart_unframe(tiny, 0));
  TEST_ASSERT_EQUAL_UINT32(0, sys_uart_unframe(tiny, sizeof(tiny)));

  /* Over CMD_FRAME_MAX bytes after COBS decode -> rejected */
  uint8_t over[CMD_FRAME_MAX + 16];
  uint32_t pos = 0;
  uint32_t left = CMD_FRAME_MAX + 1u + SYS_UART_CRC_BYTES;
  while (left > 0) {
    const uint32_t chunk = left > 254u ? 254u : left;
    over[pos++] = (uint8_t)(chunk + 1u);
    memset(over + pos, 0x33, chunk);
    pos += chunk;
    left -= chunk;
  }
  TEST_ASSERT_EQUAL_UINT32(0, sys_uart_unframe(over, pos));

  sys_uart_txq_free(&q);
}

static void test_txq_urgent_priority_raw_mode_and_clear(void) {
  sys_uart_txq_t q;
  TEST_ASSERT_TRUE(sys_uart_txq_init(&q, 512, false));

  const uint8_t n1[] = {'N', '1'};
  const uint8_t n2[] = {'N', '2'};
  const uint8_t u1[] = {'U', '1'};
  TEST_ASSERT_TRUE(sys_uart_txq_push(&q, (cmd_in_t){n1, sizeof(n1)}, CMD_SEND_NOWAIT, 0));
  TEST_ASSERT_TRUE(sys_uart_txq_push(&q, (cmd_in_t){n2, sizeof(n2)}, CMD_SEND_NOWAIT, 0));
  TEST_ASSERT_TRUE(sys_uart_txq_push(&q, (cmd_in_t){u1, sizeof(u1)}, CMD_SEND_URGENT | CMD_SEND_NOWAIT, 0));

  /* Urgent drains before normal, then FIFO among normal */
  TEST_ASSERT_EQUAL_UINT32(2, sys_uart_txq_pop(&q, 0));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(u1, q.line, 2);
  TEST_ASSERT_EQUAL_UINT32(2, sys_uart_txq_pop(&q, 0));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(n1, q.line, 2);

  /* clear drops remaining items */
  sys_uart_txq_clear(&q);
  TEST_ASSERT_EQUAL_UINT32(0, sys_uart_txq_pop(&q, 0));

  sys_uart_txq_free(&q);
}

static void test_txq_spare_reserves_half_buffer_for_non_spare(void) {
  sys_uart_txq_t q;
  /* Cap = 256 B; each 32 B raw item costs 40 B (8 + 32). Half = 128 B -> 3 SPARE items (120 B) fit, 4th (160 B) fails */
  TEST_ASSERT_TRUE(sys_uart_txq_init(&q, 256, false));
  uint8_t blk[32];
  memset(blk, 0x77, sizeof(blk));

  int spare_ok = 0;
  while (sys_uart_txq_push(&q, (cmd_in_t){blk, sizeof(blk)}, CMD_SEND_SPARE | CMD_SEND_NOWAIT, 0)) {
    spare_ok++;
  }
  TEST_ASSERT_EQUAL_INT(3, spare_ok);

  /* Non-SPARE pushes still fit into the reserved second half */
  int normal_ok = 0;
  while (sys_uart_txq_push(&q, (cmd_in_t){blk, sizeof(blk)}, CMD_SEND_NOWAIT, 0)) {
    normal_ok++;
  }
  TEST_ASSERT_EQUAL_INT(3, normal_ok);

  /* Drain all 6 and confirm queue recovers for SPARE again */
  for (int i = 0; i < 6; i++) {
    TEST_ASSERT_EQUAL_UINT32(sizeof(blk), sys_uart_txq_pop(&q, 0));
  }
  TEST_ASSERT_TRUE(sys_uart_txq_push(&q, (cmd_in_t){blk, sizeof(blk)}, CMD_SEND_SPARE | CMD_SEND_NOWAIT, 0));

  sys_uart_txq_free(&q);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_cobs_and_crc_roundtrip_edge_cases);
  RUN_TEST(test_unframe_rejects_corrupt_crc_bad_cobs_and_oversize);
  RUN_TEST(test_txq_urgent_priority_raw_mode_and_clear);
  RUN_TEST(test_txq_spare_reserves_half_buffer_for_non_spare);
  return UNITY_END();
}
