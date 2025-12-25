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
  SeqNum(uint16_t seq) : seq_(seq) {}

  bool operator==(SeqNum other) const { return seq_ == other.seq_; }

  bool operator!=(SeqNum other) const { return seq_ != other.seq_; }

  bool operator<(SeqNum other) const {
    return (int16_t)(seq_ - other.seq_) < 0;
  }

  bool operator<=(SeqNum other) const {
    return (int16_t)(seq_ - other.seq_) <= 0;
  }

  bool operator>(SeqNum other) const {
    return (int16_t)(seq_ - other.seq_) > 0;
  }

  bool operator>=(SeqNum other) const {
    return (int16_t)(seq_ - other.seq_) >= 0;
  }

  SeqNum& operator++() {
    ++seq_;
    return *this;
  }

  SeqNum operator++(int) { return SeqNum(seq_++); }

  SeqNum& operator+=(int increment) {
    seq_ += increment;
    return *this;
  }

  int operator-(SeqNum other) const { return (int16_t)(seq_ - other.seq_); }

  SeqNum operator+(int other) const { return SeqNum(seq_ + other); }
  SeqNum operator-(int other) const { return SeqNum(seq_ - other); }

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

}
