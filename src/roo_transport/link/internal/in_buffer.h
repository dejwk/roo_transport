#pragma once

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_logging.h"
#include "roo_transport/link/internal/seq_num.h"

namespace roo_transport {
namespace internal {

/// Buffered fragment of received link data.
class InBuffer {
 public:
  /// Type of content stored in the buffer.
  enum Type { kUnset, kData, kFin };
  /// Creates an empty buffer.
  InBuffer() : type_(kUnset), size_(0) {}

  /// Clears stored payload metadata.
  void clear() {
    type_ = kUnset;
    size_ = 0;
  }

  /// Stores `payload` and marks the buffer as `type`.
  void set(Type type, const roo::byte* payload, uint8_t size) {
    CHECK_LE(size, 248);
    memcpy(payload_, payload, size);
    type_ = type;
    size_ = size;
  }

  /// Returns a pointer to the stored payload.
  const roo::byte* data() const { return payload_; }
  /// Returns the stored payload type.
  Type type() const { return type_; }
  /// Returns the stored payload size.
  uint8_t size() const { return size_; }

 private:
  Type type_;
  uint8_t size_;
  roo::byte payload_[248];
};

}  // namespace internal
}  // namespace roo_transport