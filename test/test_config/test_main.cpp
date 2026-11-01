#include <cstdint>
#include <limits>
#include <unity.h>

#include "config_validation.h"

void setUp() {}
void tearDown() {}

void test_known_profile_and_supported_boundaries() {
    TEST_ASSERT_TRUE(tunnel::validLoRaSettings(906.875, 250, 11, 5, 16));
    TEST_ASSERT_TRUE(tunnel::validLoRaSettings(902, 125, 7, 5, 8));
    TEST_ASSERT_TRUE(tunnel::validLoRaSettings(928, 500, 12, 8, 64));
}

void test_large_json_integers_cannot_wrap_into_valid_profile() {
    TEST_ASSERT_FALSE(tunnel::validLoRaSettings(906.875, 250, 267, 5, 16));
    TEST_ASSERT_FALSE(tunnel::validLoRaSettings(906.875, 250, 11, 261, 16));
    TEST_ASSERT_FALSE(tunnel::validLoRaSettings(906.875, 250, 11, 5, 65552));
    TEST_ASSERT_FALSE(tunnel::validLoRaSettings(906.875, 250,
                                             std::numeric_limits<std::int64_t>::max(), 5, 16));
}

void test_negative_and_unsupported_settings_rejected() {
    TEST_ASSERT_FALSE(tunnel::validLoRaSettings(906.875, 250, -245, 5, 16));
    TEST_ASSERT_FALSE(tunnel::validLoRaSettings(906.875, 250, 11, -251, 16));
    TEST_ASSERT_FALSE(tunnel::validLoRaSettings(906.875, 250, 11, 5, -65520));
    TEST_ASSERT_FALSE(tunnel::validLoRaSettings(869.525, 250, 11, 5, 16));
    TEST_ASSERT_FALSE(tunnel::validLoRaSettings(906.875, 62.5, 11, 5, 16));
    TEST_ASSERT_FALSE(tunnel::validLoRaSettings(906.875, 250, 6, 5, 16));
    TEST_ASSERT_FALSE(tunnel::validLoRaSettings(906.875, 250, 11, 4, 16));
}

void test_nonfinite_radio_values_rejected() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    TEST_ASSERT_FALSE(tunnel::validLoRaSettings(nan, 250, 11, 5, 16));
    TEST_ASSERT_FALSE(tunnel::validLoRaSettings(inf, 250, 11, 5, 16));
    TEST_ASSERT_FALSE(tunnel::validLoRaSettings(906.875, nan, 11, 5, 16));
    TEST_ASSERT_FALSE(tunnel::validLoRaSettings(906.875, inf, 11, 5, 16));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_known_profile_and_supported_boundaries);
    RUN_TEST(test_large_json_integers_cannot_wrap_into_valid_profile);
    RUN_TEST(test_negative_and_unsupported_settings_rejected);
    RUN_TEST(test_nonfinite_radio_values_rejected);
    return UNITY_END();
}
