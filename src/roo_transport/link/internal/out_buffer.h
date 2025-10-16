#pragma once

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_transport/link/internal/seq_num.h"
#include "roo_logging.h"

namespace roo_transport {
namespace internal {

class OutBuffer {
 public:
  OutBuffer()
      : size_(0),
        acked_(false),
        flushed_(false),
        finished_(false),
        final_(false),
        expiration_(roo_time::Uptime::Start()),
        send_counter_(0) {}

  void init(SeqNum seq_id, bool control_bit);

  bool flushed() const { return flushed_; }
  bool finished() const { return finished_; }
  bool acked() const { return acked_; }

  size_t write(const roo::byte* buf, size_t count) {
    if (finished_) return 0;
    size_t capacity = 248 - size_;
    CHECK_GT(capacity, 0);
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

  void flush() { flushed_ = true; }

  void finish() {
    flushed_ = true;
    finished_ = true;
    expiration_ = roo_time::Uptime::Start();
  }

  void markFinal();

  void ack() { acked_ = true; }

  const roo::byte* data() const { return payload_; }
  const uint8_t size() const { return size_ + 2; }

  roo_time::Uptime expiration() const { return expiration_; }

  void markSent(roo_time::Uptime now);

  // Updates the timeout of the (already sent) packet to be retransmitted
  // immediately.
  void rush() {
    expiration_ = roo_time::Uptime::Start();
    CHECK_GT(send_counter_, 0);
  }

  // How many times the packet has been already sent.
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