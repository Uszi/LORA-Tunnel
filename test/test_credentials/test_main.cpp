#include <unity.h>
#include "credential_validation.h"
void setUp() {}
void tearDown() {}
void test_keyboard_punctuation_and_empty_clear() {
  TEST_ASSERT_TRUE(tunnel::validCredential(4,"Pa$$w0rd!@#%&*()[]{}:;,.?/+=-_"));
  TEST_ASSERT_TRUE(tunnel::validCredential(5,"wdk_synthetic-test_Ab9+/="));
  for(int i=0;i<6;++i) TEST_ASSERT_TRUE(tunnel::validCredential(i,""));
}
void test_network_and_header_bounds() {
  TEST_ASSERT_FALSE(tunnel::validCredential(0,"name:other"));
  TEST_ASSERT_FALSE(tunnel::validCredential(1,"token\r\nInjected: yes"));
  TEST_ASSERT_FALSE(tunnel::validCredential(4,"short"));
  char text[129]; std::memset(text,'a',128); text[128]=0;
  TEST_ASSERT_FALSE(tunnel::validCredential(5,text));
  text[127]=0; TEST_ASSERT_TRUE(tunnel::validCredential(5,text));
  text[33]=0; TEST_ASSERT_FALSE(tunnel::validCredential(3,text));
  text[32]=0; TEST_ASSERT_TRUE(tunnel::validCredential(3,text));
  std::memset(text,'a',64); text[64]=0;
  TEST_ASSERT_TRUE(tunnel::validCredential(4,text));
  text[20]='g'; TEST_ASSERT_FALSE(tunnel::validCredential(4,text));
  TEST_ASSERT_FALSE(tunnel::validCredential(-1,""));
  TEST_ASSERT_FALSE(tunnel::validCredential(6,""));
  TEST_ASSERT_FALSE(tunnel::validCredential(1,nullptr));
}
int main(int,char**) {
  UNITY_BEGIN();
  RUN_TEST(test_keyboard_punctuation_and_empty_clear);
  RUN_TEST(test_network_and_header_bounds);
  return UNITY_END();
}
