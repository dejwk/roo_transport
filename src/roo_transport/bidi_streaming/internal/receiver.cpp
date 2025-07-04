#include "roo_transport/bidi_streaming/internal/receiver.h"

#include "roo_io/memory/store.h"
#include "roo_transport/bidi_streaming/internal/protocol.h"

namespace roo_io {
namespace internal {

Receiver::Receiver(unsigned int recvbuf_log2)
    : state_(kIdle),
      peer_closed_(false),
      self_closed_(false),
      end_of_stream_(false),
      in_buffers_(new InBuffer[1 << recvbuf_log2]),
      current_in_buffer_(nullptr),
      current_in_buffer_pos_(0),
      in_ring_(recvbuf_log2, 0),
      needs_ack_(false),
      unack_seq_(0),
      recv_himark_(in_ring_.begin() + (1 << recvbuf_log2)),
      recv_himark_update_expiration_(roo_time::Uptime::Start()),
      packets_received_(0) {}

void Receiver::setConnected(SeqNum peer_seq_num) {
  CHECK(in_ring_.empty());
  in_ring_.reset(peer_seq_num);
  unack_seq_ = peer_seq_num.raw();
  state_ = kConnected;
}

void Receiver::setIdle() {
  state_ = kIdle;
  peer_closed_ = false;
  self_closed_ = false;
  end_of_stream_ = false;
}

void Receiver::setBroken() {
  state_ = kBroken;
  peer_closed_ = true;
  self_closed_ = false;
  end_of_stream_ = false;
}

bool Receiver::done() const {
  return empty() && (end_of_stream_ || self_closed_ || peer_closed_);
}

size_t Receiver::tryRead(roo::byte* buf, size_t count,
                         bool& outgoing_data_ready) {
  size_t total_read = 0;
  outgoing_data_ready = false;
  if (state_ == kConnecting || state_ == kIdle) return 0;
  do {
    if (current_in_buffer_ == nullptr) {
      if (in_ring_.empty()) {
        if (state_ == kBroken) {
          setIdle();
        }
        break;
      }
      current_in_buffer_ = &getInBuffer(in_ring_.begin());
      current_in_buffer_pos_ = 0;
    }
    if (current_in_buffer_->unset()) {
      // Not received yet.
      break;
    }
    CHECK_GE(current_in_buffer_->size(), current_in_buffer_pos_);
    size_t available = current_in_buffer_->size() - current_in_buffer_pos_;
    if (count < available) {
      memcpy(buf, current_in_buffer_->data() + current_in_buffer_pos_, count);
      total_read += count;
      current_in_buffer_pos_ += count;
      break;
    }
    memcpy(buf, current_in_buffer_->data() + current_in_buffer_pos_, available);
    buf += available;
    total_read += available;
    count -= available;
    InBuffer::Type buffer_type = current_in_buffer_->type();
    current_in_buffer_->clear();
    current_in_buffer_ = nullptr;
    recv_himark_update_expiration_ = roo_time::Uptime::Start();
    outgoing_data_ready = true;
    in_ring_.pop();
    if (buffer_type == InBuffer::kFin) {
      CHECK(in_ring_.empty()) << in_ring_.slotsUsed();
      end_of_stream_ = true;
      break;
    }
  } while (count > 0);
  return total_read;
}

void Receiver::markInputClosed(bool& outgoing_data_ready) {
  self_closed_ = true;
  if (in_ring_.empty()) return;
  recv_himark_update_expiration_ = roo_time::Uptime::Start();
  outgoing_data_ready = true;
  do {
    in_ring_.pop();
  } while (!in_ring_.empty());
}

int Receiver::peek() {
  if (current_in_buffer_ == nullptr) {
    if (in_ring_.empty()) return -1;
    current_in_buffer_ = &getInBuffer(in_ring_.begin());
    current_in_buffer_pos_ = 0;
  }
  if (current_in_buffer_->unset()) {
    // Not received yet.
    return -1;
  }
  DCHECK_GT(current_in_buffer_->size(), current_in_buffer_pos_);
  return (int)current_in_buffer_->data()[current_in_buffer_pos_];
}

size_t Receiver::availableForRead() const {
  if (current_in_buffer_ == nullptr) {
    if (in_ring_.empty()) return 0;
    current_in_buffer_ = &getInBuffer(in_ring_.begin());
    current_in_buffer_pos_ = 0;
  }
  if (current_in_buffer_->unset()) {
    // Not received yet.
    return 0;
  }
  DCHECK_GT(current_in_buffer_->size(), current_in_buffer_pos_);
  return current_in_buffer_->size() - current_in_buffer_pos_;
}

void Receiver::reset() {
  my_stream_id_ = 0;
  state_ = kIdle;
  peer_closed_ = false;
  self_closed_ = false;
  end_of_stream_ = false;
  while (!in_ring_.empty()) {
    getInBuffer(in_ring_.begin()).clear();
    in_ring_.pop();
  }
  current_in_buffer_ = nullptr;
  current_in_buffer_pos_ = 0;
  needs_ack_ = false;
  recv_himark_update_expiration_ = roo_time::Uptime::Start();
}

void Receiver::init(uint32_t my_stream_id) {
  while (!in_ring_.empty()) {
    in_ring_.pop();
  }
  my_stream_id_ = my_stream_id;
  peer_closed_ = false;
  self_closed_ = false;
  end_of_stream_ = false;
  state_ = kConnecting;
}

size_t Receiver::updateRecvHimark(roo::byte* buf, long& next_send_micros) {
  static const long kRecvHimarkExpirationTimeoutUs = 100000;
  if (state_ == kConnecting || state_ == kIdle) return 0;
  // Check if the deadline has expired to re-send the update.
  roo_time::Uptime now = roo_time::Uptime::Now();
  if (now < recv_himark_update_expiration_) {
    next_send_micros =
        std::min(next_send_micros,
                 (long)(recv_himark_update_expiration_ - now).inMicros());
    return 0;
  }
  recv_himark_ = in_ring_.begin() + in_ring_.capacity();
  uint16_t payload = FormatPacketHeader(recv_himark_, kFlowControlPacket);
  roo_io::StoreBeU16(payload, buf);
  recv_himark_update_expiration_ =
      now + roo_time::Micros(kRecvHimarkExpirationTimeoutUs);
  next_send_micros = std::min(next_send_micros, kRecvHimarkExpirationTimeoutUs);
  return 2;
}

size_t Receiver::ack(roo::byte* buf) {
  if ((state_ != kConnected && state_ != kIdle) || !needs_ack_) {
    return 0;
  }
  uint16_t payload = FormatPacketHeader(unack_seq_, kDataAckPacket);
  roo_io::StoreBeU16(payload, buf);

  // For now, we only send ack about up to 64 packets. This should be more
  // than enough in most cases. If needed, it can be extended, though; the
  // receiver will understand the arbitrary number of bytes in the bitmask,
  // not just 8.
  uint64_t ack_bitmask = 0;
  // Skipping the unack_seq_ itself, because it's status is obvious
  // (unacked).
  SeqNum in_pos = unack_seq_ + 1;
  int idx = 63;
  while (idx >= 0 && in_ring_.contains(in_pos)) {
    if (!getInBuffer(in_pos).unset()) {
      ack_bitmask |= (((uint64_t)1) << idx);
    }
    ++in_pos;
    --idx;
  }
  needs_ack_ = false;
  if (ack_bitmask == 0) {
    return 2;
  } else {
    roo_io::StoreBeU64(ack_bitmask, buf + 2);
    size_t len = 10;
    // No need to send bytes that are all zero.
    while (buf[len - 1] == roo::byte{0}) --len;
    return len;
  }
}

bool Receiver::handleDataPacket(uint16_t seq_id, const roo::byte* payload,
                                size_t len, bool is_final,
                                bool& has_new_data_to_read) {
  has_new_data_to_read = false;
  bool has_ack_to_send = false;
  if (state_ == kConnecting || state_ == kIdle) {
    return false;
  }
  SeqNum seq = in_ring_.restorePosHighBits(seq_id, 12);
  if (!in_ring_.contains(seq)) {
    if (seq < in_ring_.begin()) {
      // Retransmit of a package that was already received and read. (Maybe the
      // ack was lost.) Ignoring, but re-triggering the ack.
      needs_ack_ = true;
      has_ack_to_send = true;
      return has_ack_to_send;
    }
    if (peer_closed_) {
      // Not accepting new payload packages after having received the kFin.
      return false;
    }
    // See if we can extend the buf to add the new packet.
    size_t advance = seq - in_ring_.end() + 1;
    if (advance > in_ring_.slotsFree()) {
      // No more space; we didn't expect to receive this packet.
      return has_ack_to_send;
    }
    for (size_t i = 0; i < advance; ++i) {
      getInBuffer(in_ring_.push()).clear();
    }
    DCHECK(in_ring_.contains(seq))
        << seq << ", " << in_ring_.begin() << "--" << in_ring_.end();
  }
  InBuffer& buffer = getInBuffer(seq);
  if (buffer.unset()) {
    buffer.set(is_final ? InBuffer::kFin : InBuffer::kData, payload, len);
  } else {
    // Ignore the retransmitted packet; stick to the previously received one.
  }
  if (is_final) {
    peer_closed_ = true;
  }
  // Note: we send ack even if the packet we just received wasn't the oldest
  // unacked (i.e. even if we don't update unack_seq_), because we are
  // sending skip-acks as well.
  needs_ack_ = true;
  has_ack_to_send = true;
  if (seq == unack_seq_) {
    // Update the unack seq.
    do {
      ++unack_seq_;
      ++packets_received_;
    } while (in_ring_.contains(unack_seq_) && !getInBuffer(unack_seq_).unset());
    if (self_closed_) {
      // Remove all the received packets up to the updated unack_seq_, as if
      // they were read.
      while (!in_ring_.empty() && in_ring_.begin() < unack_seq_) {
        in_ring_.pop();
      }
    } else {
      has_new_data_to_read = true;
    }
  }
  return has_ack_to_send;
}

}  // namespace internal
}  // namespace roo_io
