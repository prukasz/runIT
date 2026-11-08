/* Host tests of the ROM bootloader's protocol (rom/sys_flasher_rom.c): SLIP, a command's packet, its checksum, an
 * answer's parse. Run: python test_host/run.py sys_flasher   (runit-esp/test_host/HOST_TESTS.MD) */
#include <string.h>
#include "unity.h"
#include "sys_flasher_rom.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- SLIP ----------------------------------------------------------------------------------------------------- */

static void test_slip_escapes_end_and_esc_between_two_ends(void) {
  const uint8_t in[] = {0x01, 0xC0, 0x02, 0xDB, 0x03};
  uint8_t out[SYS_FLASHER_SLIP_MAX(sizeof(in))];
  const uint32_t n = sys_flasher_slip_encode(in, sizeof(in), out);
  const uint8_t want[] = {0xC0, 0x01, 0xDB, 0xDC, 0x02, 0xDB, 0xDD, 0x03, 0xC0};
  TEST_ASSERT_EQUAL_UINT32(sizeof(want), n);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, sizeof(want));
}

static void test_slip_decodes_what_it_encodes_and_skips_text_before(void) {
  const uint8_t in[] = {0xC0, 0xDB, 0x00, 0x7F, 0xC0, 0xC0};
  uint8_t line[64] = "waiting for download\r\n";
  const uint32_t text = (uint32_t)strlen((const char*)line);
  const uint32_t n = text + sys_flasher_slip_encode(in, sizeof(in), line + text);
  uint8_t buf[16];
  sys_flasher_slip_rx_t rx;
  sys_flasher_slip_start(&rx, buf, sizeof(buf));
  uint32_t packets = 0;
  for (uint32_t i = 0; i < n; i++) {
    if (sys_flasher_slip_feed(&rx, line[i])) {
      packets++;
      TEST_ASSERT_EQUAL_UINT32(sizeof(in), rx.len);
      TEST_ASSERT_EQUAL_HEX8_ARRAY(in, rx.buf, sizeof(in));
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1, packets);
}

static void test_slip_two_ends_between_packets_start_the_next(void) {
  const uint8_t line[] = {0xC0, 0x11, 0xC0, 0xC0, 0x22, 0x33, 0xC0};
  uint8_t buf[8];
  sys_flasher_slip_rx_t rx;
  sys_flasher_slip_start(&rx, buf, sizeof(buf));
  uint32_t lens[2] = {0};
  uint32_t packets = 0;
  for (uint32_t i = 0; i < sizeof(line); i++) {
    if (sys_flasher_slip_feed(&rx, line[i])) {
      lens[packets++] = rx.len;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(2, packets);
  TEST_ASSERT_EQUAL_UINT32(1, lens[0]);
  TEST_ASSERT_EQUAL_UINT32(2, lens[1]);
}

static void test_slip_a_packet_longer_than_the_buffer_is_dropped(void) {
  const uint8_t line[] = {0xC0, 1, 2, 3, 4, 5, 0xC0, 0xC0, 9, 0xC0};
  uint8_t buf[3];
  sys_flasher_slip_rx_t rx;
  sys_flasher_slip_start(&rx, buf, sizeof(buf));
  uint32_t packets = 0;
  for (uint32_t i = 0; i < sizeof(line); i++) {
    if (sys_flasher_slip_feed(&rx, line[i])) {
      packets++;
      TEST_ASSERT_EQUAL_UINT32(1, rx.len);
      TEST_ASSERT_EQUAL_HEX8(9, rx.buf[0]);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1, packets);
}

/* ---- commands and answers ------------------------------------------------------------------------------------- */

static void test_checksum_xors_onto_the_seed(void) {
  const uint8_t data[] = {0x01, 0x02, 0xEF};
  TEST_ASSERT_EQUAL_UINT32(0xEFu ^ 0x01u ^ 0x02u ^ 0xEFu, sys_flasher_rom_checksum(data, sizeof(data)));
  TEST_ASSERT_EQUAL_UINT32(0xEFu, sys_flasher_rom_checksum(data, 0));
}

static void test_command_packet_is_direction_op_size_checksum_data(void) {
  const uint8_t data[] = {0xAA, 0xBB, 0xCC};
  uint8_t out[SYS_FLASHER_ROM_HEAD + sizeof(data)];
  const uint32_t n = sys_flasher_rom_command(SYS_FLASHER_ROM_FLASH_DATA, data, sizeof(data), 0x12345678u, out);
  const uint8_t want[] = {0x00, 0x03, 0x03, 0x00, 0x78, 0x56, 0x34, 0x12, 0xAA, 0xBB, 0xCC};
  TEST_ASSERT_EQUAL_UINT32(sizeof(want), n);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, sizeof(want));
}

static void test_command_data_already_in_place(void) {
  uint8_t out[SYS_FLASHER_ROM_HEAD + 2] = {0};
  out[SYS_FLASHER_ROM_HEAD] = 0x07;
  out[SYS_FLASHER_ROM_HEAD + 1] = 0x12;
  sys_flasher_rom_command(SYS_FLASHER_ROM_SYNC, out + SYS_FLASHER_ROM_HEAD, 2, 0, out);
  TEST_ASSERT_EQUAL_HEX8(0x08, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0x07, out[SYS_FLASHER_ROM_HEAD]);
  TEST_ASSERT_EQUAL_HEX8(0x12, out[SYS_FLASHER_ROM_HEAD + 1]);
}

static void test_answer_parse_takes_value_data_and_status(void) {
  const uint8_t packet[] = {0x01, 0x13, 0x06, 0x00, 0x04, 0x03, 0x02, 0x01, 'a', 'b', 0x00, 0x00, 0x00, 0x00};
  sys_flasher_rom_answer_t a;
  TEST_ASSERT_TRUE(sys_flasher_rom_parse(packet, sizeof(packet), &a));
  TEST_ASSERT_EQUAL_HEX8(0x13, a.op);
  TEST_ASSERT_EQUAL_HEX32(0x01020304u, a.value);
  TEST_ASSERT_EQUAL_UINT32(2, a.len);
  TEST_ASSERT_EQUAL_HEX8('a', a.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0, a.status);
}

static void test_answer_parse_takes_a_refusal(void) {
  const uint8_t packet[] = {0x01, 0x02, 0x04, 0x00, 0, 0, 0, 0, 0x01, 0x05, 0x00, 0x00};
  sys_flasher_rom_answer_t a;
  TEST_ASSERT_TRUE(sys_flasher_rom_parse(packet, sizeof(packet), &a));
  TEST_ASSERT_EQUAL_HEX8(1, a.status);
  TEST_ASSERT_EQUAL_HEX8(5, a.error);
  TEST_ASSERT_EQUAL_UINT32(0, a.len);
}

static void test_answer_parse_refuses_a_command_or_a_wrong_size(void) {
  const uint8_t command[] = {0x00, 0x08, 0x04, 0x00, 0, 0, 0, 0, 0, 0, 0, 0};
  const uint8_t short_size[] = {0x01, 0x08, 0x09, 0x00, 0, 0, 0, 0, 0, 0, 0, 0};
  sys_flasher_rom_answer_t a;
  TEST_ASSERT_FALSE(sys_flasher_rom_parse(command, sizeof(command), &a));
  TEST_ASSERT_FALSE(sys_flasher_rom_parse(short_size, sizeof(short_size), &a));
  TEST_ASSERT_FALSE(sys_flasher_rom_parse(command, 4, &a));
}

static void test_md5_hex_is_lowercase_two_digits_a_byte(void) {
  uint8_t digest[16];
  for (uint32_t i = 0; i < 16; i++) {
    digest[i] = (uint8_t)(i * 0x11u);
  }
  char hex[32];
  sys_flasher_md5_hex(digest, hex);
  TEST_ASSERT_EQUAL_MEMORY("00112233445566778899aabbccddeeff", hex, 32);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_slip_escapes_end_and_esc_between_two_ends);
  RUN_TEST(test_slip_decodes_what_it_encodes_and_skips_text_before);
  RUN_TEST(test_slip_two_ends_between_packets_start_the_next);
  RUN_TEST(test_slip_a_packet_longer_than_the_buffer_is_dropped);
  RUN_TEST(test_checksum_xors_onto_the_seed);
  RUN_TEST(test_command_packet_is_direction_op_size_checksum_data);
  RUN_TEST(test_command_data_already_in_place);
  RUN_TEST(test_answer_parse_takes_value_data_and_status);
  RUN_TEST(test_answer_parse_takes_a_refusal);
  RUN_TEST(test_answer_parse_refuses_a_command_or_a_wrong_size);
  RUN_TEST(test_md5_hex_is_lowercase_two_digits_a_byte);
  return UNITY_END();
}
