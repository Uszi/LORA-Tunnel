#include <unity.h>
#include "ble_ad_summary.h"

void setUp() {}
void tearDown() {}

void test_manufacturer_id_little_endian() {
  const std::uint8_t ad[]={2,1,6,3,0xff,0x4c,0};
  const auto result=tunnel::summarizeBleAd(ad,sizeof(ad));
  TEST_ASSERT_FALSE(result.malformed);
  TEST_ASSERT_TRUE(result.has_manufacturer);
  TEST_ASSERT_EQUAL_HEX16(0x004c,result.manufacturer_id);
}
void test_zero_padding_does_not_hide_scan_response() {
  const std::uint8_t ad[]={2,1,6,0,0,3,0xff,0xc8,9};
  const auto result=tunnel::summarizeBleAd(ad,sizeof(ad),5);
  TEST_ASSERT_FALSE(result.malformed);
  TEST_ASSERT_TRUE(result.has_manufacturer);
  TEST_ASSERT_EQUAL_HEX16(0x09c8,result.manufacturer_id);
  TEST_ASSERT_FALSE(tunnel::summarizeBleAd(ad,sizeof(ad)).has_manufacturer);
}
void test_ad_cannot_consume_scan_response_as_company_id() {
  const std::uint8_t ad[]={3,0xff,0x4c,0};
  const auto result=tunnel::summarizeBleAd(ad,sizeof(ad),2);
  TEST_ASSERT_TRUE(result.malformed);
  TEST_ASSERT_FALSE(result.has_manufacturer);
}
void test_malformed_second_segment_invalidates_convenience_id() {
  const std::uint8_t ad[]={3,0xff,0x4c,0,4,0xff,0xc8,9};
  const auto result=tunnel::summarizeBleAd(ad,sizeof(ad),4);
  TEST_ASSERT_TRUE(result.malformed);
  TEST_ASSERT_FALSE(result.has_manufacturer);
}
void test_last_valid_id_and_short_manufacturer_field() {
  const std::uint8_t ad[]={3,0xff,0x4c,0,3,0xff,0xc8,9,1,0xff};
  const auto result=tunnel::summarizeBleAd(ad,sizeof(ad),4);
  TEST_ASSERT_FALSE(result.malformed);
  TEST_ASSERT_TRUE(result.has_manufacturer);
  TEST_ASSERT_EQUAL_HEX16(0x09c8,result.manufacturer_id);
}
void test_invalid_inputs_and_bounds_are_rejected_without_reads() {
  const std::uint8_t ad[]={0};
  TEST_ASSERT_FALSE(tunnel::summarizeBleAd(nullptr,0).malformed);
  TEST_ASSERT_TRUE(tunnel::summarizeBleAd(nullptr,1).malformed);
  TEST_ASSERT_TRUE(tunnel::summarizeBleAd(ad,513).malformed);
  TEST_ASSERT_TRUE(tunnel::summarizeBleAd(ad,1,2).malformed);
}
int main(int,char**) {
  UNITY_BEGIN();
  RUN_TEST(test_manufacturer_id_little_endian);
  RUN_TEST(test_zero_padding_does_not_hide_scan_response);
  RUN_TEST(test_ad_cannot_consume_scan_response_as_company_id);
  RUN_TEST(test_malformed_second_segment_invalidates_convenience_id);
  RUN_TEST(test_last_valid_id_and_short_manufacturer_field);
  RUN_TEST(test_invalid_inputs_and_bounds_are_rejected_without_reads);
  return UNITY_END();
}
