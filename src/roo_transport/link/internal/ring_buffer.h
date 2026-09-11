#pragma once

#include "roo_logging.h"
#include "roo_transport/link/internal/seq_num.h"
#include "roo_transport/link/link_buffer_size.h"

namespace roo_transport {
namespace internal {

/// Circular buffer helper for wrapped `SeqNum` ranges.
///
/// The buffer has fixed size `2^capacity_log2` and is used for both incoming
/// and outgoing packet windows.
class RingBuffer {
 public:
  /// Creates a ring buffer window starting at `initial_seq`.
  RingBuffer(int capacity_log2, uint16_t initial_seq = 0)
      : capacity_log2_(capacity_log2), begin_(initial_seq), end_(initial_seq) {
    CHECK_GE(capacity_log2, 0);
    CHECK_LE(static_cast<unsigned int>(capacity_log2), kMaxLinkBufferSizeLog2);
  }

  /// Returns the number of occupied slots.
  uint16_t slotsUsed() const { return end_ - begin_; }

  /// Returns the number of free slots.
  uint16_t slotsFree() const { return capacity() - slotsUsed(); }

  /// Returns the first sequence number in the window.
  SeqNum begin() const { return begin_; }
  /// Returns one-past-the-end sequence number in the window.
  SeqNum end() const { return end_; }

  /// Extends the window by one slot and returns its sequence number.
  SeqNum push() {
    CHECK(slotsFree() > 0);
    return end_++;
  }

  /// Removes and returns the first sequence number in the window.
  SeqNum pop() {
    CHECK(slotsUsed() > 0);
    return begin_++;
  }

  /// Returns whether the window is empty.
  bool empty() const { return slotsUsed() == 0; }

  /// Resets the window to start and end at `seq`.
  void reset(SeqNum seq) {
    CHECK_EQ(begin_, end_);
    begin_ = seq;
    end_ = seq;
  }

  /// Returns whether `seq` lies within the current window.
  bool contains(SeqNum seq) const { return begin_ <= seq && seq < end_; }

  /// Returns the storage offset for `seq`.
  uint16_t offset_for(SeqNum seq) const {
    DCHECK(contains(seq));
    return seq.raw() & (capacity() - 1);
  }

  /// Restores high bits of a truncated sequence number near the current window.
  ///
  /// Chooses high bits so the reconstructed value stays close to the current
  /// window start, which is how the transport expands 12-bit on-the-wire
  /// sequence ids into the wrapped 16-bit `SeqNum` representation.
  SeqNum restorePosHighBits(uint16_t truncated_pos, int pos_bits) {
    DCHECK_GE(pos_bits, capacity_log2_ + 2);
    uint16_t left = begin_.raw() - (1 << (pos_bits - 1));
    return left + (((uint16_t)(truncated_pos - left)) % (1 << pos_bits));
  }

  /// Returns buffer capacity as a log2 value.
  int capacity_log2() const { return capacity_log2_; }
  /// Returns buffer capacity in slots.
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