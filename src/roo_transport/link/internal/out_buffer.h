#pragma once

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_logging.h"
#include "roo_transport/link/internal/seq_num.h"

namespace roo_transport {
namespace internal {

/// Buffered fragment of outbound link data awaiting transmission.
class OutBuffer {
 public:
  /// Creates an empty outbound buffer.
  OutBuffer()
      : size_(0),
        acked_(false),
        flushed_(false),
        finished_(false),
        final_(false),
        expiration_(roo_time::Uptime::Start()),
        send_counter_(0) {}

  /// Initializes packet metadata for sequence number `seq_id`.
  void init(SeqNum seq_id, bool control_bit);

  /// Returns whether the buffer has been flushed.
  bool flushed() const { return flushed_; }
  /// Returns whether the buffer is closed to further writes.
  bool finished() const { return finished_; }
  /// Returns whether the packet has been acknowledged.
  bool acked() const { return acked_; }

  /// Appends up to `count` bytes to the packet payload.
  size_t write(const roo::byte* buf, size_t count) {
    if (finished_) return 0;
    size_t capacity = 248 - size_;
    CHECK_GT(capacity, size_t{0});
    if (count >= capacity) {
      count = capacity;
      flushed_ = true;
      finished_ = true;
      expiration_ = roo_time::Uptime::Start();
    }
    memcpy(payload_ + size_ + 2, buf, count);
    size_ += count;
    return count;
  }

  /// Marks the buffer ready for sending.
  void flush() { flushed_ = true; }

  /// Marks the buffer complete and ready for sending.
  void finish() {
    flushed_ = true;
    finished_ = true;
    expiration_ = roo_time::Uptime::Start();
  }

  /// Marks this packet as the final packet in the stream.
  void markFinal();

  /// Marks the packet as acknowledged.
  void ack() { acked_ = true; }

  /// Returns a pointer to the packet bytes.
  const roo::byte* data() const { return payload_; }
  /// Returns packet size including header bytes.
  const uint8_t size() const { return size_ + 2; }

  /// Returns the scheduled retransmission time.
  roo_time::Uptime expiration() const { return expiration_; }

  /// Marks the packet as sent at time `now`.
  void markSent(roo_time::Uptime now);

  // Updates the timeout of the (already sent) packet to be retransmitted
  // immediately.
  void rush() {
    expiration_ = roo_time::Uptime::Start();
    CHECK_GT(send_counter_, 0);
  }

  /// Returns how many times the packet has already been sent.
  uint8_t send_counter() const { return send_counter_; }

 private:
  uint8_t size_;
  bool acked_;

  // Indicates that flush has been requested for this buffer, and therefore,
  // the send loop should transmit it even if it has some more space left.

  bool flushed_;
  // Indicates that no more writes are permitted for this buffer, either
  // because it is already full, or because it has already been transmitted.

  bool finished_;
  // Leave two front bytes for the header (incl. seq number).

  // Indicates that this is an 'end-of-stream' packet.
  bool final_;

  roo::byte payload_[250];

  // Set when sent, to indicate when the packet is due for retransmission.
  roo_time::Uptime expiration_;

  uint8_t send_counter_;
};

}  // namespace internal
}  // namespace roo_transport