#include <Arduino.h>

#include "gtest/gtest.h"

#define ROO_TRANSPORT_BENCHMARK_RUNS 2
#define ROO_TRANSPORT_BENCHMARK_LATENCY_SAMPLES 5
#define ROO_TRANSPORT_BENCHMARK_PAYLOAD_BYTES 16385
#define ROO_TRANSPORT_BENCHMARK_WAIT_FOR_SERIAL 0
#define setup BenchmarkExampleSetup
#define loop BenchmarkExampleLoop
#include "examples/ReliableSerial/Benchmark/Benchmark.ino"
#undef loop
#undef setup

namespace {
static_assert(ROO_TRANSPORT_BENCHMARK_PICO_DMA == 0,
              "Host integration must exercise the Arduino transport branch");
static_assert(kRuns == 2, "Exercise multiple requests before finite EOF");

// Verifies the actual sketch completes two latency/bulk cycles, validates its
// payloads including a short final block, EOF, the joined server, and released
// ESP32 UART endpoints.
TEST(BenchmarkExampleTest, FiniteLoopbackReleasesBothEndpoints) {
  const UBaseType_t original_priority = uxTaskPriorityGet(nullptr);
  BenchmarkExampleSetup();
  EXPECT_FALSE(server_thread.joinable());
  EXPECT_EQ(server_uart.get(), nullptr);
  EXPECT_EQ(client_uart.get(), nullptr);
  EXPECT_FALSE(static_cast<bool>(Serial1));
  EXPECT_FALSE(static_cast<bool>(Serial2));
  vTaskPrioritySet(nullptr, original_priority);
}
}  // namespace
