#include <Arduino.h>

#include "gtest/gtest.h"

#define setup BenchmarkExampleSetup
#define loop BenchmarkExampleLoop
#include "examples/ReliableSerial/Benchmark/Benchmark.ino"
#undef loop
#undef setup

namespace {
static_assert(kMode == Mode::kLoopback, "Test both peers on the fake board");
static_assert(16385 % kChunkSize != 0, "Exercise a partial final block");

// Verifies the actual sketch completes two latency/bulk cycles, validates its
// payloads including a short final block, EOF, the joined server, and released
// ESP32 UART endpoints.
TEST(BenchmarkExampleTest, FiniteLoopbackReleasesBothEndpoints) {
  const UBaseType_t original_priority = uxTaskPriorityGet(nullptr);
  // Pass a small finite workload without changing the sketch's constants.
  RunBenchmark(2, 16385);
  EXPECT_FALSE(server_thread.joinable());
  EXPECT_EQ(server_uart.get(), nullptr);
  EXPECT_EQ(client_uart.get(), nullptr);
  EXPECT_FALSE(static_cast<bool>(Serial1));
  EXPECT_FALSE(static_cast<bool>(Serial2));
  vTaskPrioritySet(nullptr, original_priority);
}
}  // namespace
