#include <unity.h>
#include <cmath>
#include "lora_profiles.h"
using namespace tunnel::lora_profiles;
void setUp() {}
void tearDown() {}
void test_default_frequency_slots_match_longfast_name() {
  std::uint32_t hash=5381;
  for(const char* p="LongFast";*p;++p) hash=hash*33+static_cast<unsigned char>(*p);
  TEST_ASSERT_EQUAL_UINT32(130429955,hash);
  TEST_ASSERT_EQUAL_UINT32(25,profileCount);
  for(const auto& p:profiles) {
    const unsigned slots=static_cast<unsigned>(std::floor((p.maximum_mhz-p.minimum_mhz+0.000001)/0.25));
    TEST_ASSERT_TRUE(slots>0);
    TEST_ASSERT_EQUAL_UINT16(1+hash%slots,p.frequency_slot);
    const double center=p.minimum_mhz+.125+(hash%slots)*.25;
    TEST_ASSERT_TRUE(std::fabs(center-p.frequency_mhz)<0.000001);
    TEST_ASSERT_TRUE(p.frequency_mhz-.125>=p.minimum_mhz-.000001);
    TEST_ASSERT_TRUE(p.frequency_mhz+.125<=p.maximum_mhz+.000001);
    TEST_ASSERT_EQUAL_UINT8(11,p.sf);
    TEST_ASSERT_EQUAL_UINT8(5,p.cr);
    TEST_ASSERT_EQUAL_UINT16(16,p.preamble);
    TEST_ASSERT_TRUE(p.bandwidth_khz==250);
  }
}
void test_high_band_compatibility_and_unique_region_codes() {
  unsigned compatible=0;
  for(size_t i=0;i<profileCount;++i) {
    const auto& p=profiles[i];
    TEST_ASSERT_TRUE(find(p.code)==&p);
    TEST_ASSERT_EQUAL(p.minimum_mhz>=863 && p.maximum_mhz<=928,selectable(p));
    if(selectable(p)) ++compatible;
    for(size_t j=i+1;j<profileCount;++j) TEST_ASSERT_TRUE(std::strcmp(p.code,profiles[j].code)!=0);
  }
  TEST_ASSERT_EQUAL_UINT32(18,compatible);
  TEST_ASSERT_FALSE(selectable(*find("EU_433")));
  TEST_ASSERT_FALSE(selectable(*find("CN")));
  TEST_ASSERT_TRUE(selectable(*find("EU_868")));
  TEST_ASSERT_NULL(find("UNSET")); TEST_ASSERT_NULL(find("LORA_24"));
  TEST_ASSERT_NULL(find("us")); TEST_ASSERT_NULL(find(nullptr));
  TEST_ASSERT_TRUE(isUnset("UNSET")); TEST_ASSERT_FALSE(isUnset(nullptr));
}
int main(int,char**) {
  UNITY_BEGIN();
  RUN_TEST(test_default_frequency_slots_match_longfast_name);
  RUN_TEST(test_high_band_compatibility_and_unique_region_codes);
  return UNITY_END();
}
