#pragma once

#include <cstdint>

namespace roo_transport {
namespace internal {

/// Spreads retry times by up to 20% around a capped, exponentially grown delay.
///
/// The base grows by 1.33 per retry, rounded to the nearest microsecond at each
/// step, and saturates before jitter is applied. Initial delays must be at
/// least 1000 us and no larger than the cap, which must not exceed 1000000 us;
/// growth therefore takes at most 25 steps even for a large retry count.
///
/// @p random_draw must be in [0, kRandomMax]. All of that range is normalized
/// to 8192 jitter bins, including for 32-bit hardware RNGs whose maximum
/// differs from RAND_MAX. The integer jitter radius rounds down to whole
/// microseconds; the upper endpoint is excluded, with at most 49 us between
/// bins at the largest cap. A power-of-two RNG range reduces normalization to a
/// shift.
template <uint32_t kRandomMax>
constexpr uint32_t RetryBackoffMicros(uint32_t initial_delay_us,
                                      uint32_t max_delay_us,
                                      uint32_t retry_count,
                                      uint32_t random_draw) {
  uint32_t delay = initial_delay_us;
  for (uint32_t retry = 0; retry < retry_count && delay < max_delay_us;
       ++retry) {
    delay = (delay * 133 + 50) / 100;
    if (delay > max_delay_us) delay = max_delay_us;
  }
  const uint32_t bin =
      static_cast<uint32_t>((static_cast<uint64_t>(random_draw) * 8192) /
                            (static_cast<uint64_t>(kRandomMax) + 1));
  const uint32_t radius = delay / 5;
  return delay - radius + ((bin * (2 * radius)) >> 13);
}

}  // namespace internal
}  // namespace roo_transport
