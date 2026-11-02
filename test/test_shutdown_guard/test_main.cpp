#include <cstdint>
#include <unity.h>

#include "shutdown_guard.h"

using tunnel::ShutdownDecision;
using tunnel::shutdownDecision;

void setUp() {}
void tearDown() {}

static void expect(ShutdownDecision expected,ShutdownDecision actual) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(expected),
                         static_cast<std::uint8_t>(actual));
}

void test_queued_writes_and_saving_block_shutdown() {
  expect(ShutdownDecision::Wait,shutdownDecision(false,false,false,true,true,0,0,0,0,100));
  expect(ShutdownDecision::Wait,shutdownDecision(false,true,true,true,true,0,0,0,0,100));
  expect(ShutdownDecision::Wait,shutdownDecision(false,true,false,true,true,0,0,0,0,100));
  expect(ShutdownDecision::Ready,shutdownDecision(false,false,true,true,true,0,0,0,0,100));
}

void test_active_capture_blocks_even_an_idle_logger() {
  expect(ShutdownDecision::Wait,shutdownDecision(true,false,true,true,true,0,0,0,0,100));
  expect(ShutdownDecision::Wait,shutdownDecision(true,true,true,true,true,0,0,0,0,100));
}

void test_session_storage_failure_takes_precedence_over_ready_and_timeout() {
  expect(ShutdownDecision::StorageError,shutdownDecision(false,false,true,true,false,0,0,0,0,0));
  expect(ShutdownDecision::StorageError,shutdownDecision(false,false,true,true,true,4,5,0,0,100));
  expect(ShutdownDecision::StorageError,shutdownDecision(false,false,true,true,true,0,0,4,5,100));
  expect(ShutdownDecision::StorageError,shutdownDecision(true,true,false,true,false,0,0,0,0,15000));
  expect(ShutdownDecision::StorageError,shutdownDecision(false,true,false,true,true,7,8,0,0,15000));
}

void test_no_session_can_finish_without_storage() {
  expect(ShutdownDecision::Ready,shutdownDecision(false,false,true,false,false,0,0,0,0,0));
  expect(ShutdownDecision::Ready,shutdownDecision(false,false,true,false,false,2,3,4,5,0));
  expect(ShutdownDecision::Wait,shutdownDecision(false,false,false,false,false,0,0,0,0,100));
  expect(ShutdownDecision::Wait,shutdownDecision(true,false,true,false,false,0,0,0,0,100));
}

void test_timeout_boundary_only_applies_while_pending() {
  expect(ShutdownDecision::Wait,shutdownDecision(false,true,false,true,true,0,0,0,0,14999));
  expect(ShutdownDecision::Timeout,shutdownDecision(false,true,false,true,true,0,0,0,0,15000));
  expect(ShutdownDecision::Timeout,shutdownDecision(false,false,false,true,true,0,0,0,0,15001));
  expect(ShutdownDecision::Timeout,shutdownDecision(true,false,true,true,true,0,0,0,0,15000));
  expect(ShutdownDecision::Ready,shutdownDecision(false,false,true,true,true,0,0,0,0,15000));
  expect(ShutdownDecision::Ready,shutdownDecision(false,false,true,true,true,0,0,0,0,UINT32_MAX));
}

void test_historical_unchanged_error_counts_do_not_abort() {
  expect(ShutdownDecision::Ready,shutdownDecision(false,false,true,true,true,8,8,13,13,100));
  expect(ShutdownDecision::Wait,shutdownDecision(false,true,false,true,true,8,8,13,13,100));
}

void test_wrapped_or_reset_counters_still_detect_a_change() {
  expect(ShutdownDecision::StorageError,shutdownDecision(false,false,true,true,true,UINT32_MAX,0,0,0,100));
  expect(ShutdownDecision::StorageError,shutdownDecision(false,false,true,true,true,0,0,UINT32_MAX,0,100));
  expect(ShutdownDecision::StorageError,shutdownDecision(false,false,true,true,true,9,0,0,0,100));
}

int main(int,char**) {
  UNITY_BEGIN();
  RUN_TEST(test_queued_writes_and_saving_block_shutdown);
  RUN_TEST(test_active_capture_blocks_even_an_idle_logger);
  RUN_TEST(test_session_storage_failure_takes_precedence_over_ready_and_timeout);
  RUN_TEST(test_no_session_can_finish_without_storage);
  RUN_TEST(test_timeout_boundary_only_applies_while_pending);
  RUN_TEST(test_historical_unchanged_error_counts_do_not_abort);
  RUN_TEST(test_wrapped_or_reset_counters_still_detect_a_change);
  return UNITY_END();
}
