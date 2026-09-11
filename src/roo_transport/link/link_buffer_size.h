#pragma once

namespace roo_transport {

// Twelve-bit wire sequence numbers require windows no larger than 1024
// packets to reconstruct positions unambiguously near the current window.
constexpr unsigned int kMaxLinkBufferSizeLog2 = 10;

// Approximate payload capacities; bookkeeping consumes additional memory.
enum LinkBufferSize {
  kBufferSize256B = 0,
  kBufferSize512B = 1,
  kBufferSize1KB = 2,
  kBufferSize2KB = 3,
  kBufferSize4KB = 4,
  kBufferSize8KB = 5,
  kBufferSize16KB = 6,
  kBufferSize32KB = 7,
  kBufferSize64KB = 8,
  kBufferSize128KB = 9,
  kBufferSize256KB = kMaxLinkBufferSizeLog2,
};

}  // namespace roo_transport
