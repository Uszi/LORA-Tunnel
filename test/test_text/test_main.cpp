#include <cstdint>
#include <cstring>
#include <unity.h>

#include "text_sanitize.h"

void setUp() {}
void tearDown() {}

void test_valid_utf8_survives_without_change() {
    const std::uint8_t source[]={'A',0xc3,0xa9,0xe6,0xb0,0xb4,0xf0,0x9f,0x9a,0x87};
    char target[11];
    TEST_ASSERT_FALSE(tunnel::copyUtf8Bounded(target,sizeof(target),source,sizeof(source)));
    TEST_ASSERT_EQUAL_MEMORY(source,target,sizeof(source));
    TEST_ASSERT_EQUAL_CHAR(0,target[10]);
}

void test_truncation_never_splits_a_multibyte_character() {
    const std::uint8_t source[]={'A',0xc3,0xa9,0xe6,0xb0,0xb4,0xf0,0x9f,0x9a,0x87};
    char target[10];
    TEST_ASSERT_TRUE(tunnel::copyUtf8Bounded(target,sizeof(target),source,sizeof(source)));
    TEST_ASSERT_EQUAL_UINT32(6,std::strlen(target));
    TEST_ASSERT_EQUAL_MEMORY(source,target,6);
    char narrow[2];
    TEST_ASSERT_TRUE(tunnel::copyUtf8Bounded(narrow,sizeof(narrow),source+1,2));
    TEST_ASSERT_EQUAL_STRING("",narrow);
}

void test_invalid_sequences_cannot_swallow_valid_ascii() {
    const std::uint8_t source[]={0xe2,'(',0xa1,0xc0,0xaf,0xed,0xa0,0x80,0xf4,0x90,0x80,0x80};
    char target[32];
    TEST_ASSERT_TRUE(tunnel::copyUtf8Bounded(target,sizeof(target),source,sizeof(source)));
    TEST_ASSERT_EQUAL_STRING("?(??????????",target);
}

void test_control_bytes_and_embedded_nul_are_replaced() {
    const std::uint8_t source[]={'a',0,'b','\n',0x7f,0xc2,0x85,'c'};
    char target[20];
    TEST_ASSERT_TRUE(tunnel::copyUtf8Bounded(target,sizeof(target),source,sizeof(source)));
    TEST_ASSERT_EQUAL_STRING("a?b???c",target);
}

void test_incomplete_utf8_and_destination_bounds() {
    const std::uint8_t source[]={'a',0xe2,0x82};
    char target[6]={'z','z','z','z','z','z'};
    TEST_ASSERT_TRUE(tunnel::copyUtf8Bounded(target,4,source,sizeof(source)));
    TEST_ASSERT_EQUAL_STRING("a??",target);
    TEST_ASSERT_EQUAL_CHAR('z',target[4]);
    TEST_ASSERT_EQUAL_CHAR('z',target[5]);
    char one[1]={'z'};
    TEST_ASSERT_TRUE(tunnel::copyUtf8Bounded(one,1,source,sizeof(source)));
    TEST_ASSERT_EQUAL_CHAR(0,one[0]);
}

void test_empty_and_null_inputs_are_bounded() {
    char target[4]={'z','z','z','z'};
    TEST_ASSERT_FALSE(tunnel::copyUtf8Bounded(target,sizeof(target),nullptr,0));
    TEST_ASSERT_EQUAL_STRING("",target);
    TEST_ASSERT_TRUE(tunnel::copyUtf8Bounded(target,sizeof(target),nullptr,1));
    TEST_ASSERT_TRUE(tunnel::copyUtf8Bounded(nullptr,0,nullptr,0));
    const std::uint8_t source[]={'a'};
    target[0]='z';
    TEST_ASSERT_TRUE(tunnel::copyUtf8Bounded(target,0,source,1));
    TEST_ASSERT_EQUAL_CHAR('z',target[0]);
}

int main(int,char**) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_utf8_survives_without_change);
    RUN_TEST(test_truncation_never_splits_a_multibyte_character);
    RUN_TEST(test_invalid_sequences_cannot_swallow_valid_ascii);
    RUN_TEST(test_control_bytes_and_embedded_nul_are_replaced);
    RUN_TEST(test_incomplete_utf8_and_destination_bounds);
    RUN_TEST(test_empty_and_null_inputs_are_bounded);
    return UNITY_END();
}
