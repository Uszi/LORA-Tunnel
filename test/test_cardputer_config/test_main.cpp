#define LORA_TUNNEL_CARDPUTER_ADV 1
#include <unity.h>
#include "config_document.h"

void setUp() {}
void tearDown() {}
namespace {
StaticJsonDocument<2048> doc;
bool parse(const char* json,tunnel::RadioSettings& result) {
  doc.clear();
  return !deserializeJson(doc,json) && tunnel::parseConfigRadio(doc.as<JsonVariantConst>(),result);
}
}
void test_cap_profile_range_uses_occupied_bandwidth() {
  using namespace tunnel::lora_profiles;
  unsigned count=0;
  for(const auto& p:profiles) if(selectable(p)) ++count;
  TEST_ASSERT_EQUAL_UINT32(11,count);
  for(const char* code:{"US","EU_868","ANZ","KR","RU","UA_868","MY_919","SG_923","PH_868","PH_915","BR_902"})
    TEST_ASSERT_TRUE(selectable(*find(code)));
  for(const char* code:{"JP","TW","IN","NZ_865","TH","KZ_863","NP_865","CN","EU_433"})
    TEST_ASSERT_FALSE(selectable(*find(code)));
  TEST_ASSERT_TRUE(fitsHardware(922.875,250,868,923));
  TEST_ASSERT_FALSE(fitsHardware(922.875,500,868,923));
}
void test_imported_heltec_config_preserves_values_but_disables_radio() {
  tunnel::RadioSettings result;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":true,"region":"JP"},"credentials":{"wifi_ssid":"SyntheticHome","wardrift_api_token":"synthetic-key"}})",result));
  TEST_ASSERT_FALSE(result.hardware_compatible);
  TEST_ASSERT_FALSE(result.enabled);
  TEST_ASSERT_FALSE(result.selection_complete);
  TEST_ASSERT_EQUAL_STRING("JP",result.region);
  TEST_ASSERT_TRUE(result.frequency==923.375);
  TEST_ASSERT_EQUAL_STRING("synthetic-key",doc["credentials"]["wardrift_api_token"]);
  TEST_ASSERT_TRUE(tunnel::setConfigDocumentRegion(doc.as<JsonVariant>(),"US"));
  TEST_ASSERT_TRUE(tunnel::parseConfigRadio(doc.as<JsonVariantConst>(),result));
  TEST_ASSERT_TRUE(result.enabled && result.hardware_compatible);
  TEST_ASSERT_EQUAL_STRING("SyntheticHome",doc["credentials"]["wifi_ssid"]);
}
void test_legacy_and_custom_frequencies_cannot_bypass_cap_limits() {
  tunnel::RadioSettings result;
  for(const char* json:{
      R"({"schema":1,"lora":{"enabled":true,"frequency_mhz":927}})",
      R"({"schema":1,"lora":{"enabled":true,"region":"US","frequency_mhz":923}})",
      R"({"schema":1,"lora":{"enabled":false,"region":"MY_919","frequency_mhz":923.5}})"}) {
    TEST_ASSERT_TRUE(parse(json,result));
    TEST_ASSERT_FALSE(result.enabled);
    TEST_ASSERT_FALSE(result.hardware_compatible);
  }
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":true,"frequency_mhz":915}})",result));
  TEST_ASSERT_TRUE(result.enabled && result.hardware_compatible);
}
void test_skip_and_rejected_picker_change_preserve_upload_configuration() {
  tunnel::RadioSettings result;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":false,"region":"UNSET"},"uploads":{"wardrift":true}})",result));
  TEST_ASSERT_FALSE(tunnel::setConfigDocumentRegion(doc.as<JsonVariant>(),"JP"));
  TEST_ASSERT_TRUE(tunnel::setConfigDocumentRegion(doc.as<JsonVariant>(),"UNSET"));
  TEST_ASSERT_TRUE(tunnel::parseConfigRadio(doc.as<JsonVariantConst>(),result));
  TEST_ASSERT_TRUE(result.hardware_compatible && result.selection_complete);
  TEST_ASSERT_FALSE(result.enabled);
  TEST_ASSERT_TRUE(doc["uploads"]["wardrift"].as<bool>());
}
void test_upload_switch_persists_and_rejects_malformed_sections() {
  tunnel::RadioSettings result;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":false,"region":"UNSET"},"credentials":{"wardrift_api_token":"synthetic-key"}})",result));
  TEST_ASSERT_TRUE(tunnel::setConfigDocumentUpload(doc.as<JsonVariant>(),1,true));
  char encoded[1024];serializeJson(doc,encoded,sizeof(encoded));
  TEST_ASSERT_TRUE(parse(encoded,result));
  TEST_ASSERT_TRUE(doc["uploads"]["wardrift"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("synthetic-key",doc["credentials"]["wardrift_api_token"]);
  TEST_ASSERT_TRUE(tunnel::setConfigDocumentUpload(doc.as<JsonVariant>(),1,false));
  TEST_ASSERT_FALSE(doc["uploads"]["wardrift"].as<bool>());
  for(const char* bad:{R"("bad")",R"([])",R"(true)"}) {
    char json[256];snprintf(json,sizeof(json),"{\"schema\":1,\"lora\":{\"enabled\":false},\"uploads\":%s}",bad);
    TEST_ASSERT_TRUE(parse(json,result));
    TEST_ASSERT_FALSE(tunnel::setConfigDocumentUpload(doc.as<JsonVariant>(),1,true));
  }
  TEST_ASSERT_FALSE(tunnel::setConfigDocumentUpload(doc.as<JsonVariant>(),3,true));
}
int main(int,char**) {
  UNITY_BEGIN();
  RUN_TEST(test_cap_profile_range_uses_occupied_bandwidth);
  RUN_TEST(test_imported_heltec_config_preserves_values_but_disables_radio);
  RUN_TEST(test_legacy_and_custom_frequencies_cannot_bypass_cap_limits);
  RUN_TEST(test_skip_and_rejected_picker_change_preserve_upload_configuration);
  RUN_TEST(test_upload_switch_persists_and_rejects_malformed_sections);
  return UNITY_END();
}
