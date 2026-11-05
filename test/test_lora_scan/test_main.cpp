#include <unity.h>
#include <cmath>
#include "lora_scan.h"
using namespace tunnel::lora_scan;
void setUp() {}
void tearDown() {}
void test_presets_are_region_scoped_and_snapshot_is_complete() {
  const auto* us=meshCoreProfile("US");
  TEST_ASSERT_NOT_NULL(us);
  TEST_ASSERT_EQUAL_STRING("US-NARROW",us->name);
  TEST_ASSERT_TRUE(std::fabs(us->frequency_mhz-910.525)<.000001);
  auto s=snapshot(*us,true);
  TEST_ASSERT_EQUAL_UINT8(2,s.protocol);TEST_ASSERT_EQUAL_UINT8(0x12,s.sync_word);
  TEST_ASSERT_EQUAL_UINT8(7,s.sf);TEST_ASSERT_EQUAL_UINT8(5,s.cr);
  TEST_ASSERT_TRUE(s.bandwidth_khz==62.5f);TEST_ASSERT_EQUAL_UINT16(32,s.preamble_symbols);
  TEST_ASSERT_TRUE(s.lna);TEST_ASSERT_EQUAL_STRING("US-NARROW",s.name);
  TEST_ASSERT_NULL(meshCoreProfile("US","EU_868-NARROW"));
  TEST_ASSERT_NULL(meshCoreProfile("JP"));TEST_ASSERT_NULL(meshCoreProfile("UNSET"));
  TEST_ASSERT_NULL(meshCoreProfile(nullptr));TEST_ASSERT_NULL(meshCoreProfile("US",nullptr));
  const auto* legacy=meshCoreProfile("EU_868","EU_868-LEGACY");
  TEST_ASSERT_NOT_NULL(legacy);TEST_ASSERT_EQUAL_UINT16(16,snapshot(*legacy).preamble_symbols);
  for(const auto& p:meshCoreProfiles) {
    TEST_ASSERT_NOT_NULL(meshCoreProfile(p.region,p.name));
    const auto* region=tunnel::lora_profiles::find(p.region);
    TEST_ASSERT_NOT_NULL(region);
    TEST_ASSERT_TRUE(tunnel::lora_profiles::fitsHardware(p.frequency_mhz,p.bandwidth_khz,
        region->minimum_mhz,region->maximum_mhz));
  }
}
void test_dwell_does_not_switch_before_due_or_over_completed_fifo() {
  Dwell d;d.reset(100,30000);
  TEST_ASSERT_FALSE(d.due(30099));TEST_ASSERT_TRUE(d.due(30100));
  TEST_ASSERT_EQUAL_UINT32(1,d.remaining(30099));
  TEST_ASSERT_TRUE(d.consider(30099,false,false,3000)==Dwell::Decision::Wait);
  TEST_ASSERT_TRUE(d.consider(30100,false,true,3000)==Dwell::Decision::Wait);
  TEST_ASSERT_TRUE(d.consider(30101,false,false,3000)==Dwell::Decision::Switch);
}
void test_inflight_packet_extends_dwell_but_false_preamble_is_bounded() {
  Dwell d;d.reset(0,30000);
  TEST_ASSERT_TRUE(d.consider(30000,true,false,4000)==Dwell::Decision::Wait);
  TEST_ASSERT_TRUE(d.consider(33999,true,false,4000)==Dwell::Decision::Wait);
  TEST_ASSERT_TRUE(d.consider(34000,true,false,4000)==Dwell::Decision::TimeoutSwitch);
  // Even at the timeout boundary an already complete packet has priority.
  TEST_ASSERT_TRUE(d.consider(34001,true,true,4000)==Dwell::Decision::Wait);
  TEST_ASSERT_TRUE(d.consider(34002,false,false,4000)==Dwell::Decision::Switch);
  d.reset(34002,60000);
  TEST_ASSERT_FALSE(d.due(64002));TEST_ASSERT_EQUAL_UINT32(30000,d.remaining(64002));
}
void test_millis_wrap_and_invalid_dwell_cannot_create_rapid_hopping() {
  Dwell d;const std::uint32_t start=UINT32_MAX-1000;d.reset(start,1);
  TEST_ASSERT_FALSE(d.due(start+29999));TEST_ASSERT_TRUE(d.due(start+30000));
  TEST_ASSERT_FALSE(validDwell(0));TEST_ASSERT_FALSE(validDwell(29999));
  TEST_ASSERT_TRUE(validDwell(30000));TEST_ASSERT_TRUE(validDwell(600000));
  TEST_ASSERT_FALSE(validDwell(600001));
  d.reset(start,UINT32_MAX);TEST_ASSERT_EQUAL_UINT32(30000,d.remaining(start));
}
int main(int,char**) {
  UNITY_BEGIN();
  RUN_TEST(test_presets_are_region_scoped_and_snapshot_is_complete);
  RUN_TEST(test_dwell_does_not_switch_before_due_or_over_completed_fifo);
  RUN_TEST(test_inflight_packet_extends_dwell_but_false_preamble_is_bounded);
  RUN_TEST(test_millis_wrap_and_invalid_dwell_cannot_create_rapid_hopping);
  return UNITY_END();
}
