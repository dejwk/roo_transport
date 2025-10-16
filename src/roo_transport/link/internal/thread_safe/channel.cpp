#include "roo_transport/link/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_transport/link/internal/protocol.h"
#include "roo_transport/link/internal/thread_safe/channel.h"

#if (defined ESP32 || defined ROO_TESTING)
#include "esp_random.h"
#define RANDOM_INTEGER esp_random
#else
#define RANDOM_INTEGER rand
#endif

#if !defined(MLOG_roo_transport_reliable_channel_connection)
#define MLOG_roo_transport_reliable_channel_connection 0
#endif

namespace roo_transport {

namespace {

roo_time::Duration Backoff(int retry_count) {
  float min_delay_us = 1000.0f;     // 1ms
  float max_delay_us = 1000000.0f;  // 1s
  float delay = pow(1.33, retry_count) * min_delay_us;
  if (delay > max_delay_us) {
    delay = max_delay_us;
  }
  // Randomize by +=20%, to make unrelated retries spread more evenly in time.
  delay += (float)delay * ((float)RANDOM_INTEGER() / RAND_MAX - 0.5f) * 0.4f;
  return roo_time::Micros((uint64_t)delay);
}

}  // namespace

Channel::Channel(PacketSender& sender, LinkBufferSize sendbuf,
                 LinkBufferSize recvbuf, roo::string_view name)
    : packet_sender_(sender),
      outgoing_data_ready_(),
      transmitter_((unsigned int)sendbuf),
      receiver_((unsigned int)recvbuf),
      my_stream_id_(0),
      my_stream_id_acked_by_peer_(false),
      peer_stream_id_(0),
      needs_handshake_ack_(false),
      successive_handshake_retries_(0),
      next_scheduled_handshake_update_(roo_time::Uptime::Start()),
      disconnect_fn_(nullptr),
      sender_thread_(),
      active_(true),
      name_(name),
      send_thread_name_(name_.empty() ? "send_loop" : name_ + "-send") {
  CHECK_LE((unsigned int)sendbuf, 12);
  CHECK_LE((unsigned int)sendbuf, recvbuf);
}

Channel::~Channel() { end(); }

size_t Channel::write(const roo::byte* buf, size_t count, uint32_t my_stream_id,
                      roo_io::Status& stream_status) {
  bool outgoing_data_ready = false;
  return transmitter_.write(buf, count, my_stream_id, stream_status,
                            outgoing_data_ready);
  if (outgoing_data_ready) {
    outgoing_data_ready_.notify();
  }
}

size_t Channel::tryWrite(const roo::byte* buf, size_t count,
                         uint32_t my_stream_id, roo_io::Status& stream_status) {
  bool outgoing_data_ready = false;
  return transmitter_.tryWrite(buf, count, my_stream_id, stream_status,
                               outgoing_data_ready);
  if (outgoing_data_ready) {
    outgoing_data_ready_.notify();
  }
}

size_t Channel::read(roo::byte* buf, size_t count, uint32_t my_stream_id,
                     roo_io::Status& stream_status) {
  bool outgoing_data_ready = false;
  return receiver_.read(buf, count, my_stream_id, stream_status,
                        outgoing_data_ready);
  if (outgoing_data_ready) {
    outgoing_data_ready_.notify();
  }
}

size_t Channel::tryRead(roo::byte* buf, size_t count, uint32_t my_stream_id,
                        roo_io::Status& stream_status) {
  bool outgoing_data_ready = false;
  return receiver_.tryRead(buf, count, my_stream_id, stream_status,
                           outgoing_data_ready);
  if (outgoing_data_ready) {
    outgoing_data_ready_.notify();
  }
}

int Channel::peek(uint32_t my_stream_id, roo_io::Status& stream_status) {
  return receiver_.peek(my_stream_id, stream_status);
}

size_t Channel::availableForRead(uint32_t my_stream_id,
                                 roo_io::Status& stream_status) const {
  return receiver_.availableForRead(my_stream_id, stream_status);
}

void Channel::flush(uint32_t my_stream_id, roo_io::Status& stream_status) {
  bool outgoing_data_ready = false;
  transmitter_.flush(my_stream_id, stream_status, outgoing_data_ready);
  if (outgoing_data_ready) {
    outgoing_data_ready_.notify();
  }
}

void Channel::close(uint32_t my_stream_id, roo_io::Status& stream_status) {
  bool outgoing_data_ready = false;
  transmitter_.close(my_stream_id, stream_status, outgoing_data_ready);
  if (outgoing_data_ready) {
    outgoing_data_ready_.notify();
  }
}

// Called by ChannelInput::close().
void Channel::closeInput(uint32_t my_stream_id, roo_io::Status& stream_status) {
  bool outgoing_data_ready = false;
  receiver_.markInputClosed(my_stream_id, stream_status, outgoing_data_ready);
  if (outgoing_data_ready) {
    outgoing_data_ready_.notify();
  }
}

size_t Channel::availableForWrite(uint32_t my_stream_id,
                                  roo_io::Status& stream_status) const {
  return transmitter_.availableForWrite(my_stream_id, stream_status);
}

uint32_t Channel::my_stream_id() const {
  roo::lock_guard<roo::mutex> guard(handshake_mutex_);
  return my_stream_id_;
}

uint32_t Channel::connect(std::function<void()> disconnect_fn) {
  uint32_t my_stream_id;
  std::function<void()> old_disconnect_fn;
  {
    roo::lock_guard<roo::mutex> guard(handshake_mutex_);
    old_disconnect_fn = std::move(disconnect_fn_);
    disconnect_fn_ = disconnect_fn;
    my_stream_id_ = 0;
    my_stream_id_acked_by_peer_ = false;
    peer_stream_id_ = 0;
    // The stream ID is a random number, but it can't be zero.
    while (my_stream_id_ == 0) my_stream_id_ = RANDOM_INTEGER();
    MLOG(roo_transport_reliable_channel_connection)
        << getLogPrefix() << "Transmitter and receiver are now connecting.";
    transmitter_.init(my_stream_id_, RANDOM_INTEGER() % 0x0FFF);
    receiver_.init(my_stream_id_);
    needs_handshake_ack_ = false;
    successive_handshake_retries_ = 0;
    next_scheduled_handshake_update_ = roo_time::Uptime::Start();
    connected_cv_.notify_all();
    my_stream_id = my_stream_id_;
  }
  // We need to send that handshake message.
  outgoing_data_ready_.notify();
  if (old_disconnect_fn != nullptr) {
    old_disconnect_fn();
  }
  return my_stream_id;
}

void Channel::disconnect(uint32_t my_stream_id) {
  std::function<void()> disconnect_fn;
  {
    roo::lock_guard<roo::mutex> guard(handshake_mutex_);
    if (my_stream_id_ != my_stream_id) return;
    my_stream_id_ = 0;
    my_stream_id_acked_by_peer_ = false;
    peer_stream_id_ = 0;
    MLOG(roo_transport_reliable_channel_connection)
        << getLogPrefix() << "Transmitter and receiver are now disconnected.";
    transmitter_.reset();
    receiver_.reset();
    connected_cv_.notify_all();
    disconnect_fn = std::move(disconnect_fn_);
    disconnect_fn_ = nullptr;
  }
  outgoing_data_ready_.notify();
  if (disconnect_fn != nullptr) {
    disconnect_fn();
  }
}

LinkStatus Channel::getLinkStatus(uint32_t stream_id) {
  roo::lock_guard<roo::mutex> guard(handshake_mutex_);
  return getLinkStatusInternal(stream_id);
}

LinkStatus Channel::getLinkStatusInternal(uint32_t stream_id) {
  if (stream_id == 0) return LinkStatus::kIdle;
  if (my_stream_id_ != stream_id) return LinkStatus::kBroken;
  return peer_stream_id_ == 0 || !my_stream_id_acked_by_peer_
             ? LinkStatus::kConnecting
             : LinkStatus::kConnected;
}

void Channel::awaitConnected(uint32_t stream_id) {
  roo::unique_lock<roo::mutex> guard(handshake_mutex_);
  while (getLinkStatusInternal(stream_id) == LinkStatus::kConnecting) {
    connected_cv_.wait(guard);
  }
}

bool Channel::awaitConnected(uint32_t stream_id, roo_time::Duration timeout) {
  roo::unique_lock<roo::mutex> guard(handshake_mutex_);
  roo_time::Uptime when = roo_time::Uptime::Now() + timeout;
  while (getLinkStatusInternal(stream_id) == LinkStatus::kConnecting) {
    if (connected_cv_.wait_until(guard, when) == roo::cv_status::timeout) {
      return false;
    }
  }
  return true;
}

long Channel::trySend() {
  roo::byte buf[250];
  long next_send_micros = std::numeric_limits<long>::max();
  size_t len = 0;
  len = conn(buf, next_send_micros);
  if (len > 0) {
    packet_sender_.send(buf, len);
  }
  len = receiver_.ack(buf);
  if (len > 0) {
    packet_sender_.send(buf, len);
  }
  len = receiver_.updateRecvHimark(buf, next_send_micros);
  if (len > 0) {
    packet_sender_.send(buf, len);
  }
  len = transmitter_.send(buf, next_send_micros);
  if (len > 0) {
    packet_sender_.send(buf, len);
  }
  return next_send_micros;
}

namespace {

struct HandshakePacket {
  uint16_t self_seq_num;
  uint32_t self_stream_id;
  uint32_t ack_stream_id;
  bool want_ack;

  friend roo_logging::Stream& operator<<(roo_logging::Stream& s,
                                         const HandshakePacket& p) {
    if (p.ack_stream_id == 0) {
      s << "CONN " << p.self_stream_id << ":" << p.self_seq_num;
    } else {
      s << (p.want_ack ? "CONN/ACK " : "ACK ") << p.self_stream_id << ":"
        << p.self_seq_num << "/" << p.ack_stream_id;
    }
    return s;
  }
};

}  // namespace

size_t Channel::conn(roo::byte* buf, long& next_send_micros) {
  roo::lock_guard<roo::mutex> guard(handshake_mutex_);
  auto transmitter_state = transmitter_.state();
  if (transmitter_state == internal::Transmitter::kIdle ||
      transmitter_state == internal::Transmitter::kBroken) {
    // Don't send handshake requests until we're connecting.
    return 0;
  }
  if (transmitter_state == internal::Transmitter::kConnected &&
      !needs_handshake_ack_) {
    // The handshake has already been concluded.
    return 0;
  }
  // In case we're in backoff or expect an ack, the delay is updated to reflect
  // the remaining timeout.
  long delay = std::numeric_limits<long>::max();
  if (transmitter_state == internal::Transmitter::kConnecting) {
    roo_time::Uptime now = roo_time::Uptime::Now();
    if (now < next_scheduled_handshake_update_) {
      // We do need to send a handshake packet, but not yet.
      long delay = (next_scheduled_handshake_update_ - now).inMicros();
      next_send_micros = std::min(next_send_micros, delay);
      return 0;
    }
    delay = Backoff(successive_handshake_retries_++).inMicros();
    next_scheduled_handshake_update_ = now + roo_time::Micros(delay);
  }
  // Clearing the flag, as we're about to send the handshake packet.
  needs_handshake_ack_ = false;
  bool we_need_ack = (transmitter_state != internal::Transmitter::kConnected);
  uint16_t header = FormatPacketHeader(transmitter_.front(),
                                       internal::kHandshakePacket, false);
  roo_io::StoreBeU16(header, buf);
  roo_io::StoreBeU32(my_stream_id_, buf + 2);
  roo_io::StoreBeU32(peer_stream_id_, buf + 6);
  uint8_t last_byte = we_need_ack ? 0x80 : 0x00;
  last_byte |= receiver_.buffer_size_log2();
  roo_io::StoreU8(last_byte, buf + 10);
  next_send_micros = std::min(next_send_micros, delay);
  MLOG(roo_transport_reliable_channel_connection)
      << getLogPrefix() << "Handshake packet sent: "
      << HandshakePacket{
             .self_seq_num = (uint16_t)(header & 0x0FFF),
             .self_stream_id = my_stream_id_,
             .ack_stream_id = peer_stream_id_,
             .want_ack = we_need_ack,
         };
  return 11;
}

void Channel::handleHandshakePacket(uint16_t peer_seq_num,
                                    uint32_t peer_stream_id,
                                    uint32_t ack_stream_id, bool want_ack,
                                    uint16_t peer_receive_buffer_size,
                                    bool& outgoing_data_ready) {
  std::function<void()> disconnect_fn;
  roo::lock_guard<roo::mutex> guard(handshake_mutex_);
  MLOG(roo_transport_reliable_channel_connection)
      << getLogPrefix() << "Handshake packet received: "
      << HandshakePacket{
             .self_seq_num = peer_seq_num,
             .self_stream_id = peer_stream_id,
             .ack_stream_id = ack_stream_id,
             .want_ack = want_ack,
         };
  if (peer_stream_id == my_stream_id_) {
    // The peer is echoing our own stream ID. This is probably a cross-talk from
    // our own packets.
    if (peer_seq_num == transmitter_.front().raw()) {
      MLOG(roo_transport_reliable_channel_connection)
          << getLogPrefix()
          << "Ignoring the handshake, since it's an echo of the last one we "
             "sent.";
      LOG(WARNING) << "Cross-talk detected. Please check the wiring.";
      return;
    }
  }
  switch (receiver_.state()) {
    case internal::Receiver::kConnecting: {
      if (ack_stream_id != 0 && ack_stream_id != my_stream_id_) {
        // The peer is acknowledging a different stream than the one we're
        // connecting on. Ignoring.
        MLOG(roo_transport_reliable_channel_connection)
            << getLogPrefix()
            << "Ignoring the handshake, since the ack_stream_id doesn't match.";
        break;
      }
      if (peer_stream_id == my_stream_id_) {
        // Since we ruled out the echo case above, we're going to assume that
        // this is a freakish accident of two endpoints accidentally choosing
        // the same stream ID. Bailing out, as if the peer has disconnected.
        MLOG(roo_transport_reliable_channel_connection)
            << getLogPrefix()
            << "Aborting: peer is using the same stream ID as ours: "
            << peer_stream_id;
        disconnect_fn = std::move(disconnect_fn_);
        disconnect_fn_ = nullptr;
        receiver_.setBroken();
        transmitter_.setBroken();
        my_stream_id_ = 0;
        connected_cv_.notify_all();
        break;
      }
      peer_stream_id_ = peer_stream_id;
      CHECK(receiver_.empty());
      MLOG(roo_transport_reliable_channel_connection)
          << getLogPrefix() << "Receiver is now connected.";
      receiver_.setConnected(peer_seq_num, my_control_bit());
      outgoing_data_ready = true;

      if (ack_stream_id == my_stream_id_) {
        MLOG(roo_transport_reliable_channel_connection)
            << getLogPrefix() << "Transmitter is now connected.";
        my_stream_id_acked_by_peer_ = true;
        transmitter_.setConnected(peer_receive_buffer_size, my_control_bit());
      }
      needs_handshake_ack_ = want_ack;
      connected_cv_.notify_all();
      break;
    }
    case internal::Receiver::kConnected: {
      // Note: we only consider initial connection requests as 'breaking' -
      // others might be latend acks.
      if (want_ack && (peer_stream_id_ != peer_stream_id) &&
          ack_stream_id == 0) {
        // The peer opened a new stream.
        disconnect_fn = std::move(disconnect_fn_);
        disconnect_fn_ = nullptr;
        if (!receiver_.done()) {
          MLOG(roo_transport_reliable_channel_connection)
              << getLogPrefix() << "Disconnection detected: " << peer_stream_id_
              << ", " << peer_stream_id;
          // Ignore until all in-flight packets have been delivered.
          if (transmitter_.state() == internal::Transmitter::kConnected) {
            MLOG(roo_transport_reliable_channel_connection)
                << getLogPrefix() << "Transmitter is now broken.";
            transmitter_.setBroken();
          } else {
            MLOG(roo_transport_reliable_channel_connection)
                << getLogPrefix() << "Transmitter is now idle.";
            transmitter_.reset();
          }
          my_stream_id_ = 0;
          connected_cv_.notify_all();
          MLOG(roo_transport_reliable_channel_connection)
              << getLogPrefix() << "Receiver is now broken.";
          receiver_.setBroken();
          break;
        }
        MLOG(roo_transport_reliable_channel_connection)
            << getLogPrefix() << "Transmitter and receiver are now idle.";
        transmitter_.reset();
        my_stream_id_ = 0;
        receiver_.reset();
        connected_cv_.notify_all();
        break;
      }
      if (ack_stream_id == my_stream_id_ && !my_stream_id_acked_by_peer_) {
        CHECK(my_stream_id_ != 0);
        MLOG(roo_transport_reliable_channel_connection)
            << getLogPrefix() << "Transmitter is now connected.";
        my_stream_id_acked_by_peer_ = true;
        transmitter_.setConnected(peer_receive_buffer_size, my_control_bit());
        outgoing_data_ready = true;
        connected_cv_.notify_all();
      }
      needs_handshake_ack_ = want_ack;
      break;
    }
    case internal::Receiver::kIdle:
    case internal::Receiver::kBroken: {
      // We're idle; ignoring handshake;
      MLOG(roo_transport_reliable_channel_connection)
          << getLogPrefix()
          << "Ignoring the handshake, since the receiver is not connecting.";
      break;
    }
    default: {
      break;
    }
  }
  if (disconnect_fn != nullptr) {
    disconnect_fn();
  }
}

void Channel::packetReceived(const roo::byte* buf, size_t len) {
  bool outgoing_data_ready = false;
  uint16_t header = roo_io::LoadBeU16(buf);
  bool control_bit = internal::GetPacketControlBit(header);
  auto type = internal::GetPacketType(header);
  if (type != internal::kHandshakePacket &&
      control_bit == my_control_bit()) {
    LOG(WARNING) << "Cross-talk detected. Check the wiring.";
    return;
  }
  switch (type) {
    case internal::kDataAckPacket: {
      transmitter_.ack(header & 0x0FFF, buf + 2, len - 2, outgoing_data_ready);
      break;
    }
    case internal::kFlowControlPacket: {
      // Update to available slots received.
      transmitter_.updateRecvHimark(header & 0x0FFF);
      break;
    }
    case internal::kHandshakePacket: {
      if (len != 11) {
        // Malformed packet.
        break;
      }
      uint16_t peer_seq_num = header & 0x0FFF;
      uint32_t peer_stream_id = roo_io::LoadBeU32(buf + 2);
      uint32_t ack_stream_id = roo_io::LoadBeU32(buf + 6);
      uint8_t last_byte = roo_io::LoadU8(buf + 10);
      bool want_ack = ((last_byte & 0x80) != 0);
      uint8_t peer_receive_buffer_size_log2 = last_byte & 0x0F;
      if (peer_receive_buffer_size_log2 > 12) {
        peer_receive_buffer_size_log2 = 12;
      }
      handleHandshakePacket(peer_seq_num, peer_stream_id, ack_stream_id,
                            want_ack, (1 << peer_receive_buffer_size_log2),
                            outgoing_data_ready);
      break;
    }
    case internal::kDataPacket:
    case internal::kFinPacket: {
      if (receiver_.handleDataPacket(header & 0x0FFF, buf + 2, len - 2,
                                     type == internal::kFinPacket)) {
        outgoing_data_ready = true;
      }
      break;
    }
    default: {
      // Unrecognized packet type; ignoring.
    }
  }
  if (outgoing_data_ready) {
    outgoing_data_ready_.notify();
  }
}

void Channel::sendLoop() {
  while (active_) {
    long delay_micros = trySend();
    yield();
    if (delay_micros > 0) {
      // Wait for the delay, or the notification that we have data to send,
      // whichever comes first.
      outgoing_data_ready_.await(delay_micros);
    }
  }
}

void Channel::begin() {
  roo::thread::attributes attrs;
  attrs.set_stack_size(8192);
#if (defined ESP32 || defined ROO_TESTING)
  // Set the priority just the notch below the receiver thread, so that it
  // doesn't starve the receiver thread (which receives acks) but is still high.
  attrs.set_priority(configMAX_PRIORITIES - 2);

#endif
  attrs.set_name(send_thread_name_.c_str());
  sender_thread_ = roo::thread(attrs, [this]() { sendLoop(); });
}

void Channel::end() {
  active_ = false;
  outgoing_data_ready_.notify();
  if (sender_thread_.joinable()) {
    sender_thread_.join();
  }
}

}  // namespace roo_transport

#endif  // ROO_USE_THREADS
