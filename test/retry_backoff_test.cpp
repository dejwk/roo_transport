#include "roo_transport/link/internal/retry_backoff.h"

#include <array>
#include <cstdint>
#include <limits>

#include "gtest/gtest.h"
#include "roo_time.h"
#include "roo_transport/link/internal/out_buffer.h"

namespace roo_transport {
namespace internal {
namespace {

constexpr uint32_t kFullRandomMax = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kSignedRandomMax = std::numeric_limits<int32_t>::max();
constexpr uint32_t kFullRandomMidpoint = 0x80000000u;
constexpr uint32_t kSignedRandomMidpoint = 0x40000000u;

static_assert(RetryBackoffMicros<kFullRandomMax>(5000, 200000, 1,
                                                 kFullRandomMidpoint) == 6650,
              "Backoff must support constant evaluation");

// Verifies the initial retry ordering, rounded growth, and eventual caps at
// the no-jitter midpoint for both platform random-number ranges.
TEST(RetryBackoffTest, MidpointPreservesDataAndHandshakeSchedules) {
  struct Case {
    uint32_t initial_delay;
    uint32_t cap;
    uint32_t retry;
    uint32_t expected;
  };
  constexpr std::array<Case, 8> kCases = {{{5000, 200000, 0, 5000},
                                           {5000, 200000, 1, 6650},
                                           {5000, 200000, 2, 8845},
                                           {5000, 200000, 255, 200000},
                                           {1000, 1000000, 0, 1000},
                                           {1000, 1000000, 1, 1330},
                                           {1000, 1000000, 2, 1769},
                                           {1000, 1000000, 255, 1000000}}};
  for (const Case& test : kCases) {
    SCOPED_TRACE(test.retry);
    EXPECT_EQ(
        RetryBackoffMicros<kFullRandomMax>(test.initial_delay, test.cap,
                                           test.retry, kFullRandomMidpoint),
        test.expected);
    EXPECT_EQ(
        RetryBackoffMicros<kSignedRandomMax>(test.initial_delay, test.cap,
                                             test.retry, kSignedRandomMidpoint),
        test.expected);
  }
}

// Verifies that nominal caps apply before jitter: saturated retries retain the
// full lower bound and can exceed the cap, but never reach the upper endpoint.
TEST(RetryBackoffTest, SaturatedRetriesKeepBoundedJitter) {
  EXPECT_EQ(RetryBackoffMicros<kFullRandomMax>(5000, 200000, 255, 0), 160000u);
  EXPECT_EQ(
      RetryBackoffMicros<kFullRandomMax>(5000, 200000, 255, kFullRandomMax),
      239990u);
  EXPECT_EQ(
      RetryBackoffMicros<kSignedRandomMax>(5000, 200000, 255, kSignedRandomMax),
      239990u);
  EXPECT_EQ(RetryBackoffMicros<kFullRandomMax>(1000, 1000000, 255, 0), 800000u);
  const uint32_t maximum =
      RetryBackoffMicros<kFullRandomMax>(1000, 1000000, 255, kFullRandomMax);
  EXPECT_GT(maximum, 1000000u);
  EXPECT_EQ(maximum, 1199951u);
  EXPECT_LT(maximum, 1200000u);
}

// Verifies consistent quantiles for ESP32's unsigned full-width draw and a
// signed RAND_MAX, including the largest draws without overflow or wraparound.
TEST(RetryBackoffTest, RandomRangesHaveMatchingOrderedQuantiles) {
  constexpr std::array<uint32_t, 9> kDraws = {
      0,           1,           0x12345678u, 0x3fffffffu,   0x40000000u,
      0x7fffffffu, 0x80000000u, 0xfffffffEu, kFullRandomMax};
  uint32_t previous = 0;
  for (uint32_t draw : kDraws) {
    SCOPED_TRACE(draw);
    const uint32_t full =
        RetryBackoffMicros<kFullRandomMax>(5000, 200000, 255, draw);
    const uint32_t signed_range =
        RetryBackoffMicros<kSignedRandomMax>(5000, 200000, 255, draw >> 1);
    EXPECT_EQ(full, signed_range);
    EXPECT_GE(full, previous);
    EXPECT_GE(full, 160000u);
    EXPECT_LT(full, 240000u);
    previous = full;
  }
}

// Verifies that quantization stays within 50 microseconds of continuous 20%
// jitter even at the largest nominal cap, using an independent real-valued
// reference rather than reproducing the integer-bin calculation.
TEST(RetryBackoffTest, QuantizationStaysCloseToContinuousJitter) {
  constexpr std::array<uint32_t, 7> kDraws = {
      0,           0x12345678u, 0x3fffffffu,   0x40000000u,
      0x7fffffffu, 0x80000000u, kFullRandomMax};
  for (uint32_t draw : kDraws) {
    SCOPED_TRACE(draw);
    const uint32_t delay =
        RetryBackoffMicros<kFullRandomMax>(1000, 1000000, 255, draw);
    const double ideal = 800000.0 + 400000.0 * (draw / 4294967296.0);
    EXPECT_LE(static_cast<double>(delay), ideal);
    EXPECT_LT(ideal - delay, 50.0);
  }
}

// Verifies that the first transmission increments the retry count before
// scheduling and that each subsequent expiration uses the supplied send time.
TEST(RetryBackoffTest, OutBufferStartsAtFirstRetryAndUsesSuppliedTime) {
  OutBuffer buffer;
  buffer.init(SeqNum(17), false);
  EXPECT_EQ(buffer.send_counter(), 0u);
  EXPECT_EQ(buffer.expiration(), roo_time::Uptime::Start());

  const roo_time::Uptime first =
      roo_time::Uptime::Start() + roo_time::Seconds(100);
  buffer.markSent(first);
  EXPECT_EQ(buffer.send_counter(), 1u);
  const int64_t first_delay = (buffer.expiration() - first).inMicros();
  EXPECT_GE(first_delay, 5320);
  EXPECT_LE(first_delay, 7979);

  const roo_time::Uptime second =
      roo_time::Uptime::Start() + roo_time::Seconds(200);
  buffer.markSent(second);
  EXPECT_EQ(buffer.send_counter(), 2u);
  const int64_t second_delay = (buffer.expiration() - second).inMicros();
  EXPECT_GE(second_delay, 7076);
  EXPECT_LE(second_delay, 10613);
}

// Verifies that retries beyond the byte-sized counter limit still schedule
// fresh, bounded expirations rather than wrapping to an early retry delay.
TEST(RetryBackoffTest, OutBufferCounterSaturatesAndExpirationKeepsAdvancing) {
  OutBuffer buffer;
  buffer.init(SeqNum(23), false);
  for (uint32_t attempt = 1; attempt <= 300; ++attempt) {
    SCOPED_TRACE(attempt);
    const roo_time::Uptime now =
        roo_time::Uptime::Start() + roo_time::Seconds(attempt * 2);
    buffer.markSent(now);
    EXPECT_EQ(buffer.send_counter(), attempt < 255 ? attempt : 255u);
    const int64_t delay = (buffer.expiration() - now).inMicros();
    EXPECT_GT(delay, 0);
    EXPECT_LE(delay, 239990);
    if (attempt >= 50) EXPECT_GE(delay, 160000);
  }
}

}  // namespace
}  // namespace internal
}  // namespace roo_transport
