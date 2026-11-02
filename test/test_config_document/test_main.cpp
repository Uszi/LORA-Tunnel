#include <unity.h>
#include "config_document.h"

namespace {
StaticJsonDocument<2048> document;
bool parse(const char* json, tunnel::RadioSettings& settings) {
  document.clear();
  return !deserializeJson(document, json) &&
         tunnel::parseConfigRadio(document.as<JsonVariantConst>(), settings);
}
}

void setUp() {}
void tearDown() {}

void test_legacy_enabled_config_preserves_us_profile_defaults() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":true}})", settings));
  TEST_ASSERT_TRUE(settings.enabled);
  TEST_ASSERT_TRUE(settings.frequency == 906.875);
  TEST_ASSERT_TRUE(settings.bandwidth == 250);
  TEST_ASSERT_TRUE(settings.sf == 11);
  TEST_ASSERT_TRUE(settings.cr == 5);
  TEST_ASSERT_TRUE(settings.preamble == 16);
  TEST_ASSERT_EQUAL_STRING("US",settings.region);
  TEST_ASSERT_TRUE(settings.selection_complete);
}

void test_disabled_but_valid_config_is_usable() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":false,"name":"US test","frequency_mhz":915,"bandwidth_khz":125,"spreading_factor":7,"coding_rate_denominator":8,"preamble_symbols":8,"lna":false}})", settings));
  TEST_ASSERT_FALSE(settings.enabled);
  TEST_ASSERT_TRUE(settings.frequency == 915);
  TEST_ASSERT_TRUE(settings.sf == 7);
  TEST_ASSERT_EQUAL_STRING("US",settings.region);
}
void test_fresh_and_disabled_configs_do_not_force_us_region() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_FALSE(settings.enabled);
  TEST_ASSERT_EQUAL_STRING("UNSET",settings.region);
  TEST_ASSERT_TRUE(settings.frequency==0);
  TEST_ASSERT_FALSE(settings.selection_complete);
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":false}})",settings));
  TEST_ASSERT_EQUAL_STRING("UNSET",settings.region);
  TEST_ASSERT_FALSE(settings.selection_complete);
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":false,"region":"UNSET","frequency_mhz":0}})",settings));
  TEST_ASSERT_FALSE(parse(R"({"schema":1,"lora":{"enabled":true,"region":"UNSET"}})",settings));
  TEST_ASSERT_FALSE(parse(R"({"schema":1,"lora":{"enabled":false,"region":"UNSET","frequency_mhz":906.875}})",settings));
}
void test_named_regions_select_defaults_and_keep_disabled_choice() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":true,"region":"EU_868"}})",settings));
  TEST_ASSERT_EQUAL_STRING("EU_868",settings.region);
  TEST_ASSERT_TRUE(settings.frequency==869.525);
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":false,"region":"JP"}})",settings));
  TEST_ASSERT_FALSE(settings.enabled);
  TEST_ASSERT_EQUAL_STRING("JP",settings.region);
  TEST_ASSERT_TRUE(settings.frequency==923.375);
}
void test_unknown_incompatible_and_out_of_region_settings_rejected() {
  tunnel::RadioSettings settings;
  for(const char* json:{
      R"({"schema":1,"lora":{"enabled":true,"region":"us"}})",
      R"({"schema":1,"lora":{"enabled":true,"region":"LORA_24"}})",
      R"({"schema":1,"lora":{"enabled":true,"region":4}})",
      R"({"schema":1,"lora":{"enabled":true,"region":"EU_433"}})",
      R"({"schema":1,"lora":{"enabled":true,"region":"CN"}})",
      R"({"schema":1,"lora":{"enabled":true,"region":"EU_868","frequency_mhz":906.875}})",
      R"({"schema":1,"lora":{"enabled":true,"region":"EU_868","bandwidth_khz":500}})",
      R"({"schema":1,"lora":{"enabled":true,"region":"US","frequency_mhz":902}})"
    }) TEST_ASSERT_FALSE(parse(json,settings));
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":false,"region":"EU_433"}})",settings));
  TEST_ASSERT_FALSE(settings.enabled);
}
void test_region_changes_preserve_credentials_custom_fields_and_round_trip() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":true,"frequency_mhz":915,"future_radio":"keep"},"credentials":{"wigle_api_name":"sample-name","wigle_api_token":"synthetic-token","wdgwars_token":"synthetic-mesh","wifi_ssid":"ExampleAP","wifi_password":"synthetic-pass"},"schedule":{"wifi_dwell_ms":222},"future":{"value":42}})",settings));
  TEST_ASSERT_TRUE(tunnel::setConfigDocumentRegion(document.as<JsonVariant>(),"EU_868"));
  TEST_ASSERT_EQUAL_STRING("synthetic-token",document["credentials"]["wigle_api_token"]);
  TEST_ASSERT_EQUAL_STRING("sample-name",document["credentials"]["wigle_api_name"]);
  TEST_ASSERT_EQUAL_STRING("synthetic-mesh",document["credentials"]["wdgwars_token"]);
  TEST_ASSERT_EQUAL_STRING("ExampleAP",document["credentials"]["wifi_ssid"]);
  TEST_ASSERT_EQUAL_STRING("synthetic-pass",document["credentials"]["wifi_password"]);
  TEST_ASSERT_EQUAL_STRING("keep",document["lora"]["future_radio"]);
  TEST_ASSERT_EQUAL_INT(222,document["schedule"]["wifi_dwell_ms"]);
  TEST_ASSERT_EQUAL_INT(42,document["future"]["value"]);
  char serialized[2048];
  const size_t length=serializeJson(document,serialized,sizeof(serialized));
  TEST_ASSERT_TRUE(length>0 && length<sizeof(serialized));
  TEST_ASSERT_TRUE(parse(serialized,settings));
  TEST_ASSERT_EQUAL_STRING("EU_868",settings.region);
  TEST_ASSERT_TRUE(settings.enabled && settings.frequency==869.525);
  // A later keyboard save changes a credential while retaining the region.
  document["credentials"]["wigle_api_token"]="replacement-example";
  TEST_ASSERT_TRUE(tunnel::parseConfigRadio(document.as<JsonVariantConst>(),settings));
  TEST_ASSERT_EQUAL_STRING("EU_868",settings.region);
  TEST_ASSERT_TRUE(tunnel::setConfigDocumentRegion(document.as<JsonVariant>(),"UNSET"));
  TEST_ASSERT_TRUE(tunnel::parseConfigRadio(document.as<JsonVariantConst>(),settings));
  TEST_ASSERT_FALSE(settings.enabled);
  TEST_ASSERT_EQUAL_STRING("UNSET",settings.region);
  TEST_ASSERT_TRUE(settings.selection_complete);
  TEST_ASSERT_EQUAL_STRING("replacement-example",document["credentials"]["wigle_api_token"]);
}
void test_explicit_skip_survives_sd_round_trip_without_nvs() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":false,"region":"UNSET","selection_complete":false}})",settings));
  TEST_ASSERT_FALSE(settings.selection_complete);
  TEST_ASSERT_TRUE(tunnel::setConfigDocumentRegion(document.as<JsonVariant>(),"UNSET"));
  char saved[512];
  TEST_ASSERT_TRUE(serializeJson(document,saved,sizeof(saved))<sizeof(saved));
  TEST_ASSERT_TRUE(parse(saved,settings));
  TEST_ASSERT_TRUE(settings.selection_complete);
  TEST_ASSERT_FALSE(settings.enabled);
  TEST_ASSERT_EQUAL_STRING("UNSET",settings.region);
  TEST_ASSERT_TRUE(settings.frequency==0);
  TEST_ASSERT_FALSE(parse(R"({"schema":1,"lora":{"enabled":false,"region":"UNSET","selection_complete":"true"}})",settings));
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":true,"frequency_mhz":915}})",settings));
  TEST_ASSERT_TRUE(settings.selection_complete);
  TEST_ASSERT_EQUAL_STRING("US",settings.region);
}
void test_rejected_region_change_leaves_document_unchanged() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":true,"region":"US"},"credentials":{"wigle_api_token":"synthetic-token"}})",settings));
  char before[256],after[256];
  serializeJson(document,before,sizeof(before));
  TEST_ASSERT_FALSE(tunnel::setConfigDocumentRegion(document.as<JsonVariant>(),"CN"));
  TEST_ASSERT_FALSE(tunnel::setConfigDocumentRegion(document.as<JsonVariant>(),nullptr));
  serializeJson(document,after,sizeof(after));
  TEST_ASSERT_EQUAL_STRING(before,after);
}

void test_parseable_invalid_primary_is_not_usable_or_rotatable() {
  tunnel::RadioSettings settings;
  settings.frequency = 920;
  TEST_ASSERT_FALSE(parse(R"({"schema":1,"lora":{"enabled":"broken"}})", settings));
  TEST_ASSERT_TRUE(settings.frequency == 920);
  // The same reader must still accept the valid backup after that rejection.
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":true,"frequency_mhz":906.875}})", settings));
  TEST_ASSERT_TRUE(settings.frequency == 906.875);
}

void test_schema_marker_does_not_make_missing_radio_valid() {
  tunnel::RadioSettings settings;
  const char* invalid[] = {
    R"({"schema":1})", R"({"schema":1,"lora":null})",
    R"({"schema":1,"lora":{}})", R"({"schema":"1","lora":{"enabled":true}})",
    R"({"schema":2,"lora":{"enabled":true}})", R"([])"
  };
  for (const char* json : invalid) TEST_ASSERT_FALSE(parse(json, settings));
}

void test_invalid_typed_and_out_of_range_radio_fields_are_rejected() {
  tunnel::RadioSettings settings;
  const char* invalid[] = {
    R"({"schema":1,"lora":{"enabled":true,"frequency_mhz":"906.875"}})",
    R"({"schema":1,"lora":{"enabled":true,"frequency_mhz":null}})",
    R"({"schema":1,"lora":{"enabled":true,"frequency_mhz":869.525}})",
    R"({"schema":1,"lora":{"enabled":true,"bandwidth_khz":62.5}})",
    R"({"schema":1,"lora":{"enabled":true,"spreading_factor":11.5}})",
    R"({"schema":1,"lora":{"enabled":true,"spreading_factor":267}})",
    R"({"schema":1,"lora":{"enabled":false,"coding_rate_denominator":261}})",
    R"({"schema":1,"lora":{"enabled":false,"preamble_symbols":65552}})"
  };
  for (const char* json : invalid) TEST_ASSERT_FALSE(parse(json, settings));
}

void test_invalid_optional_radio_fields_are_rejected() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_FALSE(parse(R"({"schema":1,"lora":{"enabled":true,"lna":"false"}})", settings));
  TEST_ASSERT_FALSE(parse(R"({"schema":1,"lora":{"enabled":true,"name":42}})", settings));
  TEST_ASSERT_FALSE(parse(R"({"schema":1,"lora":{"enabled":true,"name":"123456789012345678901234567890123456789012"}})", settings));
}

void test_unknown_fields_do_not_invalidate_forward_compatible_config() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":true,"future_radio_field":123},"future_feature":{"enabled":true}})", settings));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_legacy_enabled_config_preserves_us_profile_defaults);
  RUN_TEST(test_disabled_but_valid_config_is_usable);
  RUN_TEST(test_fresh_and_disabled_configs_do_not_force_us_region);
  RUN_TEST(test_named_regions_select_defaults_and_keep_disabled_choice);
  RUN_TEST(test_unknown_incompatible_and_out_of_region_settings_rejected);
  RUN_TEST(test_region_changes_preserve_credentials_custom_fields_and_round_trip);
  RUN_TEST(test_rejected_region_change_leaves_document_unchanged);
  RUN_TEST(test_explicit_skip_survives_sd_round_trip_without_nvs);
  RUN_TEST(test_parseable_invalid_primary_is_not_usable_or_rotatable);
  RUN_TEST(test_schema_marker_does_not_make_missing_radio_valid);
  RUN_TEST(test_invalid_typed_and_out_of_range_radio_fields_are_rejected);
  RUN_TEST(test_invalid_optional_radio_fields_are_rejected);
  RUN_TEST(test_unknown_fields_do_not_invalidate_forward_compatible_config);
  return UNITY_END();
}
