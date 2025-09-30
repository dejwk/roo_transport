#pragma once

namespace roo_transport {

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
  kBufferSize256KB = 10,
  kBufferSize512KB = 11,
  kBufferSize1MB = 12,
};

}  // namespace roo_transport
