// Exercise both generic configuration validation and the narrower Cardputer
// import policy. Existing test_config_document covers Heltec region behavior.
#define LORA_TUNNEL_CARDPUTER_ADV 1
#include <unity.h>
#include "config_document.h"
#include <cstring>

namespace {
StaticJsonDocument<4096> document;
bool parse(const char* json,tunnel::RadioSettings& result) {
  document.clear();return !deserializeJson(document,json) &&
      tunnel::parseConfigRadio(document.as<JsonVariantConst>(),result);
}
bool keys(const char* json,tunnel::mesh::Key* output,uint8_t& count,bool& publicDefault) {
  document.clear();return !deserializeJson(document,json) &&
      tunnel::parseMeshKeys(document.as<JsonVariantConst>(),output,count,publicDefault);
}
}
void setUp() {}
void tearDown() {}
void test_legacy_defaults_and_explicit_three_modes() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":true,"region":"US"}})",settings));
  TEST_ASSERT_EQUAL_UINT8(0,settings.mode);TEST_ASSERT_EQUAL_UINT32(30000,settings.dwell_ms);
  TEST_ASSERT_EQUAL_STRING("AUTO",settings.meshcore_profile);
  for(unsigned i=0;i<3;++i) {
    document["lora"]["mode"]=tunnel::loraModeName(i);
    TEST_ASSERT_TRUE(tunnel::parseConfigRadio(document.as<JsonVariantConst>(),settings));
    TEST_ASSERT_EQUAL_UINT8(i,settings.mode);
  }
}
void test_invalid_modes_dwell_types_and_bounds() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":true,"region":"US","mode":"combined","dwell_ms":600000}})",settings));
  TEST_ASSERT_EQUAL_UINT32(600000,settings.dwell_ms);
  for(const char* mode:{"Combined","both","", "meshtastic+meshcore"}) {
    document["lora"]["mode"]=mode;
    TEST_ASSERT_FALSE(tunnel::parseConfigRadio(document.as<JsonVariantConst>(),settings));
  }
  document["lora"]["mode"]="combined";
  for(long long dwell:{-1LL,0LL,29999LL,600001LL,4294967296LL}) {
    document["lora"]["dwell_ms"]=dwell;
    TEST_ASSERT_FALSE(tunnel::parseConfigRadio(document.as<JsonVariantConst>(),settings));
  }
  document["lora"]["dwell_ms"]="30000";
  TEST_ASSERT_FALSE(tunnel::parseConfigRadio(document.as<JsonVariantConst>(),settings));
  document["lora"]["dwell_ms"]=30000.5;
  TEST_ASSERT_FALSE(tunnel::parseConfigRadio(document.as<JsonVariantConst>(),settings));
  TEST_ASSERT_FALSE(parse(R"({"schema":1,"lora":{"enabled":true,"region":"US","mode":"combined\u0000suffix"}})",settings));
}
void test_region_specific_meshcore_selection_and_disabled_config() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":true,"region":"US","mode":"combined","meshcore_profile":"US-NARROW"}})",settings));
  TEST_ASSERT_TRUE(settings.enabled);TEST_ASSERT_EQUAL_STRING("US-NARROW",settings.meshcore_profile);
  TEST_ASSERT_FALSE(parse(R"({"schema":1,"lora":{"enabled":true,"region":"US","mode":"combined","meshcore_profile":"EU_868-NARROW"}})",settings));
  TEST_ASSERT_FALSE(parse(R"({"schema":1,"lora":{"enabled":true,"region":"RU","mode":"meshcore"}})",settings));
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":false,"region":"UNSET","mode":"combined"}})",settings));
  TEST_ASSERT_FALSE(settings.enabled);TEST_ASSERT_FALSE(settings.selection_complete);
  TEST_ASSERT_FALSE(parse(R"({"schema":1,"lora":{"enabled":true,"region":"US","mode":"combined","meshcore_profile":"AUTO\u0000suffix"}})",settings));
}
void test_imported_heltec_profile_disables_cap_without_discarding_document() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":true,"region":"US","mode":"combined","meshcore_profile":"US-SOCAL"},"credentials":{"wigle_api_token":"synthetic-placeholder"}})",settings));
  TEST_ASSERT_FALSE(settings.hardware_compatible);TEST_ASSERT_FALSE(settings.enabled);
  TEST_ASSERT_FALSE(settings.selection_complete);TEST_ASSERT_EQUAL_STRING("US-SOCAL",settings.meshcore_profile);
  TEST_ASSERT_EQUAL_STRING("synthetic-placeholder",document["credentials"]["wigle_api_token"]);
}
void test_keys_default_public_and_empty_collection() {
  tunnel::mesh::Key output[tunnel::mesh::MaxKeys];uint8_t count=99;bool publicDefault=false;
  TEST_ASSERT_TRUE(keys("{}",output,count,publicDefault));TEST_ASSERT_TRUE(publicDefault);TEST_ASSERT_EQUAL_UINT8(0,count);
  TEST_ASSERT_TRUE(keys(R"({"decode_public":false,"meshtastic_keys":[]})",output,count,publicDefault));
  TEST_ASSERT_FALSE(publicDefault);TEST_ASSERT_EQUAL_UINT8(0,count);
  TEST_ASSERT_FALSE(keys(R"({"decode_public":"false"})",output,count,publicDefault));
  TEST_ASSERT_FALSE(keys(R"({"meshtastic_keys":{}})",output,count,publicDefault));
}
void test_default_public_key_hash_and_plaintext_channel() {
  tunnel::mesh::Key output[4];uint8_t count=0;bool publicDefault=true;
  TEST_ASSERT_TRUE(keys(R"({"meshtastic_keys":[{"name":"LongFast","key_hex":"d4f1bb3a20290759f0bcffabcf4e6901"},{"name":"A","key_hex":""}]})",output,count,publicDefault));
  TEST_ASSERT_EQUAL_UINT8(2,count);TEST_ASSERT_EQUAL_UINT8(16,output[0].length);
  TEST_ASSERT_EQUAL_HEX8(8,output[0].channel_hash);TEST_ASSERT_EQUAL_HEX8(0xd4,output[0].bytes[0]);
  TEST_ASSERT_EQUAL_UINT8(0,output[1].length);TEST_ASSERT_EQUAL_HEX8('A',output[1].channel_hash);
  TEST_ASSERT_TRUE(tunnel::parseMeshKeys(document.as<JsonVariantConst>(),nullptr,count,publicDefault));
  TEST_ASSERT_EQUAL_UINT8(2,count); // Validation-only pass must match actual loading.
}
void test_aes256_hex_case_and_key_count_bound() {
  document.clear();document["decode_public"]=false;
  auto entries=document.createNestedArray("meshtastic_keys");
  for(unsigned i=0;i<4;++i){auto e=entries.createNestedObject();e["name"]="TEST";e["key_hex"]="000102030405060708090A0B0C0D0E0F101112131415161718191a1b1c1d1e1f";}
  tunnel::mesh::Key output[4];uint8_t count=0;bool publicDefault=true;
  TEST_ASSERT_TRUE(tunnel::parseMeshKeys(document.as<JsonVariantConst>(),output,count,publicDefault));
  TEST_ASSERT_EQUAL_UINT8(4,count);TEST_ASSERT_EQUAL_UINT8(32,output[0].length);TEST_ASSERT_EQUAL_HEX8(0x0a,output[0].bytes[10]);
  entries.add(entries[0]);
  TEST_ASSERT_FALSE(tunnel::parseMeshKeys(document.as<JsonVariantConst>(),output,count,publicDefault));
}
void test_malformed_key_names_hex_and_nuls_rejected() {
  tunnel::mesh::Key output[4];uint8_t count=0;bool publicDefault=true;
  const char* invalid[]={
    R"({"meshtastic_keys":[{"name":"","key_hex":""}]})",
    R"({"meshtastic_keys":[{"name":"A\u0000B","key_hex":""}]})",
    R"({"meshtastic_keys":[{"name":"A","key_hex":"00\u0000"}]})",
    R"({"meshtastic_keys":[{"name":"A","key_hex":"AQ=="}]})",
    R"({"meshtastic_keys":[{"name":"A","key_hex":"0"}]})",
    R"({"meshtastic_keys":[{"name":"A","key_hex":"gg112233445566778899aabbccddeeff"}]})",
    R"({"meshtastic_keys":[{"name":"A"}]})",
    R"({"meshtastic_keys":[{"name":5,"key_hex":""}]})"
  };
  for(const auto* json:invalid)TEST_ASSERT_FALSE(keys(json,output,count,publicDefault));
  document.clear();auto e=document["meshtastic_keys"].createNestedObject();e["name"]="0123456789012345678901234567890123456789";e["key_hex"]="";
  TEST_ASSERT_FALSE(tunnel::parseMeshKeys(document.as<JsonVariantConst>(),output,count,publicDefault));
}
void test_region_change_resets_mesh_profile_and_preserves_keys() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":true,"region":"US","mode":"combined","meshcore_profile":"US-NARROW","dwell_ms":90000,"decode_public":false,"meshtastic_keys":[{"name":"BENCH","key_hex":""}]}})",settings));
  TEST_ASSERT_TRUE(tunnel::setConfigDocumentRegion(document.as<JsonVariant>(),"EU_868"));
  TEST_ASSERT_EQUAL_STRING("AUTO",document["lora"]["meshcore_profile"]);
  TEST_ASSERT_EQUAL_STRING("combined",document["lora"]["mode"]);
  TEST_ASSERT_EQUAL_UINT32(90000,document["lora"]["dwell_ms"]);
  TEST_ASSERT_FALSE(document["lora"]["decode_public"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("BENCH",document["lora"]["meshtastic_keys"][0]["name"]);
  TEST_ASSERT_TRUE(tunnel::setConfigDocumentRegion(document.as<JsonVariant>(),"RU"));
  TEST_ASSERT_EQUAL_STRING("meshtastic",document["lora"]["mode"]);
  TEST_ASSERT_TRUE(tunnel::parseConfigRadio(document.as<JsonVariantConst>(),settings));
}
int main(int,char**) {
  UNITY_BEGIN();
  RUN_TEST(test_legacy_defaults_and_explicit_three_modes);
  RUN_TEST(test_invalid_modes_dwell_types_and_bounds);
  RUN_TEST(test_region_specific_meshcore_selection_and_disabled_config);
  RUN_TEST(test_imported_heltec_profile_disables_cap_without_discarding_document);
  RUN_TEST(test_keys_default_public_and_empty_collection);
  RUN_TEST(test_default_public_key_hash_and_plaintext_channel);
  RUN_TEST(test_aes256_hex_case_and_key_count_bound);
  RUN_TEST(test_malformed_key_names_hex_and_nuls_rejected);
  RUN_TEST(test_region_change_resets_mesh_profile_and_preserves_keys);
  return UNITY_END();
}
