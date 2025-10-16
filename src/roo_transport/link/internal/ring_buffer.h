#pragma once

#include "roo_transport/link/internal/seq_num.h"
#include "roo_logging.h"

namespace roo_transport {
namespace internal {

// Circular buffer implementation for 16-bit sequence numbers.
// The buffer is implemented as a ring buffer, with a fixed size of
// 2^capacity_log2.
//
// Used for both incoming and outgoing data.
class RingBuffer {
 public:
  RingBuffer(int capacity_log2, uint16_t initial_seq = 0)
      : capacity_log2_(capacity_log2), begin_(initial_seq), end_(initial_seq) {
    CHECK_LE(capacity_log2, 10);
  }

  uint16_t slotsUsed() const { return end_ - begin_; }

  uint16_t slotsFree() const { return capacity() - slotsUsed(); }

  SeqNum begin() const { return begin_; }
  SeqNum end() const { return end_; }

  SeqNum push() {
    CHECK(slotsFree() > 0);
    return end_++;
  }

  SeqNum pop() {
    CHECK(slotsUsed() > 0);
    return begin_++;
  }

  bool empty() const { return slotsUsed() == 0; }

  void reset(SeqNum seq) {
    CHECK_EQ(begin_, end_);
    begin_ = seq;
    end_ = seq;
  }

  // Relies on wrap-around semantics of SeqNum.
  bool contains(SeqNum seq) const { return begin_ <= seq && seq < end_; }

  uint16_t offset_for(SeqNum seq) const {
    DCHECK(contains(seq));
    return seq.raw() & (capacity() - 1);
  }

  // Restores high bits of seq, extending it to uint16_t, by assuming that
  // truncated_pos must be 'close' to the range. Specifically, we make sure to
  // pick high bits so that the result is within 1 << (pos_bits/2) from
  // begin.
  SeqNum restorePosHighBits(uint16_t truncated_pos, int pos_bits) {
    DCHECK_GE(pos_bits, capacity_log2_ + 2);
    uint16_t left = begin_.raw() - (1 << (pos_bits - 1));
    return left + (((uint16_t)(truncated_pos - left)) % (1 << pos_bits));
  }

  int capacity_log2() const { return capacity_log2_; }
  uint16_t capacity() const { return 1 << capacity_log2_; }

 private:
  uint16_t offset_begin() const { return begin_.raw() & (capacity() - 1); }
  uint16_t offset_end() const { return end_.raw() & (capacity() - 1); }

  int capacity_log2_;
  SeqNum begin_;
  SeqNum end_;
};

}  // namespace internal
}  // namespace roo_transport