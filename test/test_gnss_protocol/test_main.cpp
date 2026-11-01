#include <cstring>
#include <initializer_list>
#include <unity.h>
#include "gnss_protocol.h"

using namespace tunnel::gnss_protocol;
void setUp() {}
void tearDown() {}

void test_documented_checksum_examples() {
  const char output[]="PCAS03,1,1,1,1,1,1,1,1,0,0,,,0,0";
  const char warning[]="GPTXT,01,01,01,ANTENNA OPEN";
  TEST_ASSERT_EQUAL_HEX8(0x02,nmeaChecksum(output,sizeof(output)-1));
  TEST_ASSERT_EQUAL_HEX8(0x25,nmeaChecksum(warning,sizeof(warning)-1));
}
void test_diagnostic_command_preserves_reserved_fields_and_line_endings() {
  char output[64]{};
  TEST_ASSERT_EQUAL_UINT32(38,diagnosticCommand(output,sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("$PCAS03,1,0,1,2,1,0,0,1,0,0,,,0,0*00\r\n",output);
}
void test_command_rejects_small_or_null_buffers_without_partial_output() {
  char output[64]; std::memset(output,'x',sizeof(output));
  TEST_ASSERT_EQUAL_UINT32(0,diagnosticCommand(nullptr,sizeof(output)));
  for(std::size_t size=0;size<=kDiagnosticCommandSize;++size) {
    TEST_ASSERT_EQUAL_UINT32(0,diagnosticCommand(output,size));
    for(char value:output) TEST_ASSERT_EQUAL_CHAR('x',value);
  }
  TEST_ASSERT_EQUAL_UINT32(kDiagnosticCommandSize,diagnosticCommand(output,kDiagnosticCommandSize+1));
  TEST_ASSERT_EQUAL_CHAR(0,output[kDiagnosticCommandSize]);
  TEST_ASSERT_EQUAL_CHAR('x',output[kDiagnosticCommandSize+1]);
}
void test_antenna_warning_requires_exact_documented_text() {
  TEST_ASSERT_TRUE(antennaOpen("ANTENNA OPEN",12));
  TEST_ASSERT_FALSE(antennaOpen(nullptr,12));
  TEST_ASSERT_FALSE(antennaOpen("ANTENNA OPEN",11));
  TEST_ASSERT_FALSE(antennaOpen("ANTENNA OPEN EXTRA",18));
  TEST_ASSERT_FALSE(antennaOpen("ANTENNA OK",10));
  TEST_ASSERT_FALSE(antennaOpen("ANTENNA SHORT",13));
  TEST_ASSERT_FALSE(antennaOpen("antenna open",12));
  TEST_ASSERT_EQUAL_INT8(static_cast<int>(AntennaStatus::Ok),static_cast<int>(antennaStatus("ANTENNA OK",10)));
  TEST_ASSERT_EQUAL_INT8(static_cast<int>(AntennaStatus::Unknown),static_cast<int>(antennaStatus("ANTENNA OK extra",16)));
}

static GsvFields fields(const char* text) {
  GsvFields out;
  std::uint16_t term=1;
  const char* start=text;
  for(const char* p=text;;++p) {
    if(*p==',' || *p==0) {
      out.field(term++,start,static_cast<std::size_t>(p-start));
      if(!*p) break;
      start=p+1;
    }
  }
  return out;
}
void test_complete_gsv_group_accumulates_cn0_instead_of_latest_fragment_only() {
  SkyTracker sky; SkyView rows[kSkyTalkers];
  // Quectel protocol section2.2.3 example data, without NMEA framing.
  sky.accept("GP",fields("3,1,12,02,49,123,43,05,60,005,43,06,06,127,29,07,05,042,33,0"),100);
  sky.accept("GP",fields("3,2,12,13,79,139,44,15,55,225,44,18,12,314,36,20,44,055,42,0"),110);
  sky.snapshot(rows,120);
  TEST_ASSERT_EQUAL_UINT32(0,rows[0].complete_groups);
  sky.accept("GP",fields("3,3,12,29,48,275,42,30,20,069,38,193,69,062,42,195,46,160,42,0"),120);
  sky.snapshot(rows,150);
  TEST_ASSERT_EQUAL_UINT32(1,rows[0].complete_groups);
  TEST_ASSERT_EQUAL_INT(12,rows[0].visible);
  TEST_ASSERT_EQUAL_INT(44,rows[0].max_cn0);
  TEST_ASSERT_EQUAL_UINT8(12,rows[0].cn0_count);
  TEST_ASSERT_EQUAL_UINT8(12,rows[0].positive_cn0_count);
  TEST_ASSERT_EQUAL_UINT32(30,rows[0].age_ms);
}
void test_blank_cn0_is_unknown_and_zero_cn0_is_reported_separately() {
  SkyTracker sky; SkyView rows[kSkyTalkers];
  sky.accept("GP",fields("1,1,02,01,10,100,,02,20,200,0,0"),10);
  sky.snapshot(rows,10);
  TEST_ASSERT_EQUAL_INT(2,rows[0].visible);
  TEST_ASSERT_EQUAL_UINT8(1,rows[0].cn0_count);
  TEST_ASSERT_EQUAL_UINT8(0,rows[0].positive_cn0_count);
  TEST_ASSERT_EQUAL_INT(0,rows[0].max_cn0);
  sky.accept("GL",fields("1,1,01,70,10,100,,0"),11);
  sky.snapshot(rows,11);
  TEST_ASSERT_EQUAL_INT(-1,rows[3].max_cn0);
  TEST_ASSERT_EQUAL_UINT8(0,rows[3].cn0_count);
}
void test_talkers_are_separate_and_empty_group_is_available_zero() {
  SkyTracker sky; SkyView rows[kSkyTalkers];
  sky.accept("GP",fields("1,1,01,01,10,100,35,0"),10);
  sky.accept("BD",fields("1,1,00,0"),20);
  sky.accept("GL",fields("1,1,01,70,10,100,22,0"),30);
  sky.snapshot(rows,40);
  TEST_ASSERT_EQUAL_STRING("GP",rows[0].talker);
  TEST_ASSERT_EQUAL_INT(35,rows[0].max_cn0);
  TEST_ASSERT_EQUAL_UINT32(30,rows[0].age_ms);
  TEST_ASSERT_EQUAL_STRING("BD",rows[2].talker);
  TEST_ASSERT_EQUAL_INT(0,rows[2].visible);
  TEST_ASSERT_EQUAL_INT(-1,rows[2].max_cn0);
  TEST_ASSERT_EQUAL_INT(22,rows[3].max_cn0);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,rows[1].age_ms);
}
void test_out_of_order_and_changed_signal_do_not_commit_partial_groups() {
  SkyTracker sky; SkyView rows[kSkyTalkers];
  const auto first=fields("2,1,05,01,10,100,40,02,10,100,39,03,10,100,38,04,10,100,37,0");
  const auto last=fields("2,2,05,05,10,100,30,0");
  sky.accept("GP",last,10);
  sky.accept("GP",first,20);
  sky.accept("GP",fields("2,2,05,05,10,100,30,1"),30);
  sky.snapshot(rows,40);
  TEST_ASSERT_EQUAL_UINT32(0,rows[0].complete_groups);
  sky.accept("GP",first,50);
  sky.accept("GP",last,60);
  sky.snapshot(rows,70);
  TEST_ASSERT_EQUAL_UINT32(1,rows[0].complete_groups);
  TEST_ASSERT_EQUAL_INT(40,rows[0].max_cn0);
}
void test_incomplete_new_cycle_preserves_prior_complete_group_and_age() {
  SkyTracker sky; SkyView rows[kSkyTalkers];
  sky.accept("GB",fields("1,1,01,01,10,100,35,0"),10);
  sky.accept("GB",fields("2,1,05,01,10,100,40,02,10,100,39,03,10,100,38,04,10,100,37,0"),20);
  sky.snapshot(rows,100);
  TEST_ASSERT_EQUAL_INT(1,rows[1].visible);
  TEST_ASSERT_EQUAL_INT(35,rows[1].max_cn0);
  TEST_ASSERT_EQUAL_UINT32(90,rows[1].age_ms);
}
void test_malformed_and_oversize_gsv_fields_never_commit() {
  SkyTracker sky; SkyView rows[kSkyTalkers];
  for(const char* input:{"1,1,01,01,10,100,100,0", "1,1,01,01,10,100,x,0",
                        "1,1,01,01,10,100,9999999,0", "1,1,01,01,10,100,35,0,0",
                        "1,2,01,01,10,100,35,0", "0,1,01,01,10,100,35,0"})
    sky.accept("GP",fields(input),10);
  sky.snapshot(rows,20);
  TEST_ASSERT_EQUAL_UINT32(0,rows[0].complete_groups);
  TEST_ASSERT_EQUAL_INT(-1,rows[0].visible);
}
void test_group_timeout_and_millis_wrap() {
  SkyTracker sky; SkyView rows[kSkyTalkers];
  const auto first=fields("2,1,05,01,10,100,40,02,10,100,39,03,10,100,38,04,10,100,37,0");
  const auto last=fields("2,2,05,05,10,100,30,0");
  sky.accept("GN",first,10); sky.accept("GN",last,5011);
  sky.snapshot(rows,5012); TEST_ASSERT_EQUAL_UINT32(0,rows[4].complete_groups);
  sky.accept("GN",first,UINT32_MAX-10); sky.accept("GN",last,5);
  sky.snapshot(rows,15);
  TEST_ASSERT_EQUAL_UINT32(1,rows[4].complete_groups);
  TEST_ASSERT_EQUAL_UINT32(10,rows[4].age_ms);
}
int main(int,char**) {
  UNITY_BEGIN();
  RUN_TEST(test_documented_checksum_examples);
  RUN_TEST(test_diagnostic_command_preserves_reserved_fields_and_line_endings);
  RUN_TEST(test_command_rejects_small_or_null_buffers_without_partial_output);
  RUN_TEST(test_antenna_warning_requires_exact_documented_text);
  RUN_TEST(test_complete_gsv_group_accumulates_cn0_instead_of_latest_fragment_only);
  RUN_TEST(test_blank_cn0_is_unknown_and_zero_cn0_is_reported_separately);
  RUN_TEST(test_talkers_are_separate_and_empty_group_is_available_zero);
  RUN_TEST(test_out_of_order_and_changed_signal_do_not_commit_partial_groups);
  RUN_TEST(test_incomplete_new_cycle_preserves_prior_complete_group_and_age);
  RUN_TEST(test_malformed_and_oversize_gsv_fields_never_commit);
  RUN_TEST(test_group_timeout_and_millis_wrap);
  return UNITY_END();
}
