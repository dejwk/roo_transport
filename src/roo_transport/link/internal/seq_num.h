#pragma once

#include <cstdint>

#include "roo_logging.h"

namespace roo_transport {
namespace internal {

// This class implements a sequence number that wraps around at 2^16.
// It is used to track the sequence numbers of packets in a reliable
// streaming protocol. The sequence number is represented as a 16-bit
// unsigned integer, and the class provides various comparison and
// arithmetic operations to facilitate the handling of sequence numbers
// in a circular buffer context. The class also provides methods to
// increment the sequence number and to convert it to its raw
// representation.
//
// The usage relies on the fact that in the context of a reliable
// streaming protocol, at any given time, the sequence numbers are expected to
// be within a certain reception window, up to 2^10 in size (typically below
// 2^8). Wrap-around is handled on this basis.
//
// In practice, the streaming protocol uses 12-bit sequence IDs, so we should in
// fact wrap around at 2^12, rather than 2^16. However, we keep the 16-bit
// representation for simplicity. (The ring buffer class has a helper method to
// convert 12-bit sequence IDs to 16-bit, again on the basis of the fact that
// the values are expected to be contained in a small range).
class SeqNum {
 public:
  /// Creates a wrapped sequence number from its raw representation.
  SeqNum(uint16_t seq) : seq_(seq) {}

  /// Returns whether two sequence numbers are equal.
  bool operator==(SeqNum other) const { return seq_ == other.seq_; }

  /// Returns whether two sequence numbers differ.
  bool operator!=(SeqNum other) const { return seq_ != other.seq_; }

  /// Returns whether this sequence number precedes `other`.
  bool operator<(SeqNum other) const {
    return (int16_t)(seq_ - other.seq_) < 0;
  }

  /// Returns whether this sequence number precedes or equals `other`.
  bool operator<=(SeqNum other) const {
    return (int16_t)(seq_ - other.seq_) <= 0;
  }

  /// Returns whether this sequence number follows `other`.
  bool operator>(SeqNum other) const {
    return (int16_t)(seq_ - other.seq_) > 0;
  }

  /// Returns whether this sequence number follows or equals `other`.
  bool operator>=(SeqNum other) const {
    return (int16_t)(seq_ - other.seq_) >= 0;
  }

  /// Increments the sequence number.
  SeqNum& operator++() {
    ++seq_;
    return *this;
  }

  /// Returns the previous value and then increments the sequence number.
  SeqNum operator++(int) { return SeqNum(seq_++); }

  /// Advances the sequence number by `increment`.
  SeqNum& operator+=(int increment) {
    seq_ += increment;
    return *this;
  }

  /// Returns wrapped distance from `other` to this value.
  int operator-(SeqNum other) const { return (int16_t)(seq_ - other.seq_); }

  /// Returns sequence number advanced by `other`.
  SeqNum operator+(int other) const { return SeqNum(seq_ + other); }
  /// Returns sequence number moved back by `other`.
  SeqNum operator-(int other) const { return SeqNum(seq_ - other); }

  /// Returns the raw 16-bit representation.
  uint16_t raw() const { return seq_; }

 private:
  uint16_t seq_;
};

}  // namespace internal
}  // namespace roo_transport

namespace roo_logging {

inline roo_logging::Stream& operator<<(roo_logging::Stream& os,
                                       roo_transport::internal::SeqNum seq) {
  os << seq.raw();
  return os;
}

}  // namespace roo_logging
