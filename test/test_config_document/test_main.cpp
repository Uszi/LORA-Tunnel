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

void test_minimal_enabled_config_uses_confirmed_profile_defaults() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":true}})", settings));
  TEST_ASSERT_TRUE(settings.enabled);
  TEST_ASSERT_TRUE(settings.frequency == 906.875);
  TEST_ASSERT_TRUE(settings.bandwidth == 250);
  TEST_ASSERT_TRUE(settings.sf == 11);
  TEST_ASSERT_TRUE(settings.cr == 5);
  TEST_ASSERT_TRUE(settings.preamble == 16);
}

void test_disabled_but_valid_config_is_usable() {
  tunnel::RadioSettings settings;
  TEST_ASSERT_TRUE(parse(R"({"schema":1,"lora":{"enabled":false,"name":"US test","frequency_mhz":915,"bandwidth_khz":125,"spreading_factor":7,"coding_rate_denominator":8,"preamble_symbols":8,"lna":false}})", settings));
  TEST_ASSERT_FALSE(settings.enabled);
  TEST_ASSERT_TRUE(settings.frequency == 915);
  TEST_ASSERT_TRUE(settings.sf == 7);
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
  RUN_TEST(test_minimal_enabled_config_uses_confirmed_profile_defaults);
  RUN_TEST(test_disabled_but_valid_config_is_usable);
  RUN_TEST(test_parseable_invalid_primary_is_not_usable_or_rotatable);
  RUN_TEST(test_schema_marker_does_not_make_missing_radio_valid);
  RUN_TEST(test_invalid_typed_and_out_of_range_radio_fields_are_rejected);
  RUN_TEST(test_invalid_optional_radio_fields_are_rejected);
  RUN_TEST(test_unknown_fields_do_not_invalidate_forward_compatible_config);
  return UNITY_END();
}
