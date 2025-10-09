#include "roo_transport/link/internal/transmitter.h"

#include "roo_backport.h"
#include "roo_backport/byte.h"

namespace roo_transport {
namespace internal {

Transmitter::Transmitter(unsigned int sendbuf_log2)
    : state_(kIdle),
      end_of_stream_(false),
      out_buffers_(new OutBuffer[1 << sendbuf_log2]),
      current_out_buffer_(nullptr),
      out_ring_(sendbuf_log2, 0),
      next_to_send_(out_ring_.begin()),
      recv_himark_(out_ring_.begin() + (1 << sendbuf_log2)),
      has_pending_eof_(false),
      packets_sent_(0),
      packets_delivered_(0) {}

size_t Transmitter::tryWrite(const roo::byte* buf, size_t count,
                             bool& outgoing_data_ready) {
  outgoing_data_ready = false;
  if (count == 0) return 0;
  if (end_of_stream_) return 0;
  if (state_ == kIdle || state_ == kBroken) return 0;
  size_t total_written = 0;
  do {
    CHECK_GE(recv_himark_, out_ring_.end());
    if (current_out_buffer_ == nullptr) {
      if (recv_himark_ == out_ring_.end()) {
        // No more tokens.
        break;
      }
      if (out_ring_.slotsFree() == 0) {
        break;
      }
      SeqNum pos = out_ring_.push();
      current_out_buffer_ = &getOutBuffer(pos);
      current_out_buffer_->init(pos);
    }
    size_t written = current_out_buffer_->write(buf, count);
    total_written += written;
    buf += written;
    count -= written;
    if (current_out_buffer_->finished()) {
      current_out_buffer_ = nullptr;
      outgoing_data_ready = true;
    }

  } while (count > 0);
  return total_written;
}

bool Transmitter::flush() {
  if (current_out_buffer_ != nullptr) {
    current_out_buffer_->flush();
    return true;
  }
  return false;
}

bool Transmitter::hasPendingData() const { return !out_ring_.empty(); }

void Transmitter::addEosPacket() {
  SeqNum pos = out_ring_.push();
  auto* buf = &getOutBuffer(pos);
  buf->init(pos);
  buf->markFinal();
  buf->finish();
}

void Transmitter::close() {
  if (state_ == kBroken) {
    state_ = kIdle;
  }
  if (end_of_stream_ || state_ == kIdle) {
    return;
  }
  flush();
  if (out_ring_.slotsFree() == 0) {
    has_pending_eof_ = true;
  } else {
    addEosPacket();
  }
  end_of_stream_ = true;
}

void Transmitter::setBroken() {
  while (!out_ring_.empty()) {
    out_ring_.pop();
  }
  state_ = kBroken;
}

size_t Transmitter::availableForWrite() const {
  if (end_of_stream_ || state_ == kIdle || state_ == kBroken) {
    return 0;
  }
  // In the extreme case, if flush is issued after every write, we might only
  // fit one byte per slot.
  return out_ring_.slotsFree();
}

size_t Transmitter::send(roo::byte* buf, long& next_send_micros) {
  const internal::OutBuffer* buf_to_send = getBufferToSend(next_send_micros);
  if (buf_to_send == nullptr) return 0;
  const roo::byte* data = buf_to_send->data();
  size_t size = buf_to_send->size();
  memcpy(buf, data, size);
  return size;
}

const internal::OutBuffer* Transmitter::getBufferToSend(
    long& next_send_micros) {
  if (state_ != kConnected) return nullptr;
  if (out_ring_.contains(next_to_send_)) {
    // Best-effort attempt to quickly send the next buffer in the sequence.
    OutBuffer& buf = getOutBuffer(next_to_send_);
    if (!buf.acked() && buf.flushed()) {
      if (!buf.finished()) buf.finish();
      if (buf.send_counter() == 0) {
        // Never sent before.
        ++next_to_send_;
        ++packets_sent_;
        buf.markSent(roo_time::Uptime::Now());
        next_send_micros = 0;
        return &buf;
      }
    }
  }
  // Fall back: find the earliest finished buffer to send.
  roo_time::Uptime now = roo_time::Uptime::Now();
  SeqNum to_send = out_ring_.end();
  roo_time::Uptime min_send_time = roo_time::Uptime::Max();
  for (SeqNum pos = out_ring_.begin();
       pos < out_ring_.end() && pos < recv_himark_; ++pos) {
    OutBuffer& buf = getOutBuffer(pos);
    if (buf.acked()) {
      continue;
    }
    if (!buf.flushed()) {
      // No more ready to send buffers can follow.
      break;
    }
    if (!buf.finished() || buf.expiration() == roo_time::Uptime::Start()) {
      // This one can be sent immediately; no need to seek any further.
      to_send = pos;
      min_send_time = roo_time::Uptime::Start();
      break;
    }
    // This is a viable candidate.
    if (buf.expiration() < min_send_time) {
      to_send = pos;
      min_send_time = buf.expiration();
    }
  }
  if (!out_ring_.contains(to_send)) {
    // // No more packets to send at all.
    // // Auto-flush: let's see if we can opportunistically close and send a
    // // packet?
    // if (out_ring_.slotsUsed() == 1 && out_ring_.begin() < recv_himark_) {
    //   OutBuffer& buf = getOutBuffer(out_ring_.begin());
    //   if (!buf.finished())
    //   DCHECK(!buf.flushed());
    //   DCHECK(!buf.acked());
    //   DCHECK_GT(buf.size(), 0);
    //   buf.finish();
    //   to_send = out_ring_.begin();
    //   min_send_time = roo_time::Uptime::Start();
    // } else {
      return nullptr;
    // }
  }
  if (min_send_time > now) {
    // The next packet to (re)send is not ready yet.
    next_send_micros =
        std::min(next_send_micros, (long)(min_send_time - now).inMicros());
    return nullptr;
  }

  OutBuffer& buf = getOutBuffer(to_send);
  if (!buf.finished()) {
    buf.finish();
  }
  buf.markSent(now);
  next_to_send_ = to_send + 1;
  ++packets_sent_;
  next_send_micros = 0;
  return &buf;
}

void Transmitter::reset() {
  while (!out_ring_.empty()) {
    out_ring_.pop();
  }
  end_of_stream_ = false;
  my_stream_id_ = 0;
  state_ = kIdle;
  current_out_buffer_ = nullptr;
  has_pending_eof_ = false;
}

void Transmitter::init(uint32_t my_stream_id, SeqNum new_start) {
  my_stream_id_ = my_stream_id;
  state_ = kConnecting;
  end_of_stream_ = false;
  while (!out_ring_.empty()) {
    out_ring_.pop();
  }
  out_ring_.reset(new_start);
  recv_himark_ = out_ring_.begin() + out_ring_.capacity();
  next_to_send_ = out_ring_.begin();
  current_out_buffer_ = nullptr;
  has_pending_eof_ = false;
}

bool Transmitter::ack(uint16_t seq_id, const roo::byte* ack_bitmap,
                      size_t ack_bitmap_len) {
  // Remove all buffers up to the acked position.
  SeqNum seq = out_ring_.restorePosHighBits(seq_id, 12);
  while (out_ring_.begin() < seq && !out_ring_.empty()) {
    out_ring_.pop();
    ++packets_delivered_;
    if (has_pending_eof_) {
      // Process that pending EOF, now that we have space.
      addEosPacket();
      has_pending_eof_ = false;
    }
  }
  if (out_ring_.empty()) {
    if (end_of_stream_) {
      reset();
    }
    return false;
  }
  // Process the skip-ack notifications.
  size_t offset = 0;
  SeqNum out_pos = out_ring_.begin() + 1;
  SeqNum last_acked = out_ring_.begin() - 1;
  while (offset < ack_bitmap_len) {
    uint8_t val = (uint8_t)ack_bitmap[offset];
    for (int i = 7; i >= 0; --i) {
      if (out_ring_.contains(out_pos) && (val & (1 << i)) != 0) {
        getOutBuffer(out_pos).ack();
        last_acked = out_pos;
      }
      out_pos++;
    }
    offset++;
  }
  bool rushed = false;
  // Try to increase send throughput by quickly detecting dropped packets,
  // interpreting skip-ack as nack for packets that have only been sent once
  // (which means that, assuming in-order delivery of the underlying package
  // writer, if they were to be delivered, they would have been already
  // delivered).
  if (out_ring_.contains(last_acked)) {
    bool next_to_send_updated = false;
    // Rush re-delivery of any packets that have only been sent once and
    // nacked.
    for (SeqNum pos = out_ring_.begin(); pos != last_acked; ++pos) {
      auto& buf = getOutBuffer(pos);
      if (!buf.acked() && buf.send_counter() == 1) {
        buf.rush();
        rushed = true;
        // Also, send the first nacked packet ASAP, to unblock the reader.
        if (!next_to_send_updated) {
          next_to_send_updated = true;
          next_to_send_ = pos;
        }
      }
    }
  }
  return rushed;
}

}  // namespace internal
}  // namespace roo_transport
