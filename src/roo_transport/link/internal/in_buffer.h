#pragma once

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_transport/link/internal/seq_num.h"
#include "roo_logging.h"

namespace roo_transport {
namespace internal {

class InBuffer {
 public:
  enum Type { kUnset, kData, kFin };
  InBuffer() : type_(kUnset), size_(0) {}

  void clear() {
    type_ = kUnset;
    size_ = 0;
  }

  void set(Type type, const roo::byte* payload, uint8_t size) {
    CHECK_LE(size, 248);
    memcpy(payload_, payload, size);
    type_ = type;
    size_ = size;
  }

  // Returns true if the buffer's payload has not yet been received.
  bool unset() const { return type_ == kUnset; }

  const roo::byte* data() const { return payload_; }
  Type type() const { return type_; }
  uint8_t size() const { return size_; }

 private:
  Type type_;
  uint8_t size_;
  roo::byte payload_[248];
};

}  // namespace internal
}  // namespace roo_transport