/* Host tests of core/nvs_store (chunked JSON file, CRC32, offsets, reopen persistence, erase). */
#include <string.h>

#include "host_assert.h"
#include "nvs_store.h"
#include "unity.h"

extern void host_nvs_store_reset(void);

void setUp(void) {
  host_nvs_store_reset();
}

void tearDown(void) {}

static void test_json_append_info_get_reopen_and_erase(void) {
  nvs_store_json_t f;
  TEST_ASSERT_NULL(nvs_store_json_open(&f, "nvs", "board"));
  nvs_store_json_info_t info = nvs_store_json_info(&f);
  TEST_ASSERT_EQUAL_UINT32(0, info.bytes);
  TEST_ASSERT_EQUAL_UINT32(0, info.crc32);
  TEST_ASSERT_EQUAL_UINT16(0, info.chunks);

  /* Standard zlib CRC-32 test vector split across 2 chunks: "1234" + "56789" -> 0xCBF43926 */
  const uint8_t c0[] = "1234";
  const uint8_t c1[] = "56789";
  TEST_ASSERT_NULL(nvs_store_json_append(&f, 0, c0, 4));
  TEST_ASSERT_NULL(nvs_store_json_append(&f, 4, c1, 5));

  info = nvs_store_json_info(&f);
  TEST_ASSERT_EQUAL_UINT32(9, info.bytes);
  TEST_ASSERT_EQUAL_UINT16(2, info.chunks);
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, info.crc32);

  /* Read chunks back */
  uint8_t buf[16];
  uint32_t len = 0;
  TEST_ASSERT_NULL(nvs_store_json_get(&f, 0, buf, sizeof(buf), &len));
  TEST_ASSERT_EQUAL_UINT32(4, len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(c0, buf, 4);

  TEST_ASSERT_NULL(nvs_store_json_get(&f, 1, buf, sizeof(buf), &len));
  TEST_ASSERT_EQUAL_UINT32(5, len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(c1, buf, 5);

  /* Past last chunk -> ERR_NVS_NO_CHUNK */
  err_h e = nvs_store_json_get(&f, 2, buf, sizeof(buf), &len);
  TEST_ASSERT_ERR_TAG(ERR_NVS_NO_CHUNK, e);
  TEST_ASSERT_EQUAL_UINT16(2, ERR_DATA(e, ERR_NVS_NO_CHUNK)->chunk);
  err_free(e);

  /* Reopen in fresh handle preserves metadata and chunks */
  nvs_store_json_t f2;
  TEST_ASSERT_NULL(nvs_store_json_open(&f2, "nvs", "board"));
  info = nvs_store_json_info(&f2);
  TEST_ASSERT_EQUAL_UINT32(9, info.bytes);
  TEST_ASSERT_EQUAL_UINT16(2, info.chunks);
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, info.crc32);

  /* Erase clears info and chunks */
  TEST_ASSERT_NULL(nvs_store_json_erase(&f2));
  info = nvs_store_json_info(&f2);
  TEST_ASSERT_EQUAL_UINT32(0, info.bytes);
  TEST_ASSERT_EQUAL_UINT16(0, info.chunks);
  e = nvs_store_json_get(&f2, 0, buf, sizeof(buf), &len);
  TEST_ASSERT_ERR_TAG(ERR_NVS_NO_CHUNK, e);
  err_free(e);
}

static void test_json_validation_bad_arg_and_offset(void) {
  nvs_store_json_t f;
  TEST_ASSERT_NULL(nvs_store_json_open(&f, "nvs", "cfg"));

  const uint8_t data[] = {1, 2, 3};
  err_h e = nvs_store_json_append(&f, 0, NULL, 3);
  TEST_ASSERT_ERR_TAG(ERR_BAD_ARG, e);
  err_free(e);

  e = nvs_store_json_append(&f, 0, data, 0);
  TEST_ASSERT_ERR_TAG(ERR_BAD_ARG, e);
  err_free(e);

  e = nvs_store_json_append(&f, 5, data, 3);
  TEST_ASSERT_ERR_TAG(ERR_BAD_OFFSET, e);
  TEST_ASSERT_EQUAL_UINT32(5, ERR_DATA(e, ERR_BAD_OFFSET)->got);
  TEST_ASSERT_EQUAL_UINT32(0, ERR_DATA(e, ERR_BAD_OFFSET)->want);
  err_free(e);

  TEST_ASSERT_NULL(nvs_store_json_append(&f, 0, data, 3));
  e = nvs_store_json_append(&f, 2, data, 3);
  TEST_ASSERT_ERR_TAG(ERR_BAD_OFFSET, e);
  TEST_ASSERT_EQUAL_UINT32(2, ERR_DATA(e, ERR_BAD_OFFSET)->got);
  TEST_ASSERT_EQUAL_UINT32(3, ERR_DATA(e, ERR_BAD_OFFSET)->want);
  err_free(e);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_json_append_info_get_reopen_and_erase);
  RUN_TEST(test_json_validation_bad_arg_and_offset);
  return UNITY_END();
}
