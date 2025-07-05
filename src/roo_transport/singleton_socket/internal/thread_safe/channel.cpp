#include "roo_transport/singleton_socket/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_transport/singleton_socket/internal/protocol.h"
#include "roo_transport/singleton_socket/internal/thread_safe/channel.h"

namespace roo_transport {

namespace {

roo_time::Interval Backoff(int retry_count) {
  float min_delay_us = 1000.0f;       // 1ms
  float max_delay_us = 100000000.0f;  // 100ms
  float delay = pow(1.33, retry_count) * min_delay_us;
  if (delay > max_delay_us) {
    delay = max_delay_us;
  }
  // Randomize by +=20%, to make unrelated retries spread more evenly in time.
  delay += (float)delay * ((float)rand() / RAND_MAX - 0.5f) * 0.4f;
  return roo_time::Micros((uint64_t)delay);
}

}  // namespace

Channel::Channel(PacketSender& sender, PacketReceiver& receiver,
                 unsigned int sendbuf_log2, unsigned int recvbuf_log2)
    : packet_sender_(sender),
      packet_receiver_(receiver),
      outgoing_data_ready_(),
      transmitter_(sendbuf_log2, outgoing_data_ready_),
      receiver_(recvbuf_log2, outgoing_data_ready_),
      my_stream_id_(0),
      peer_stream_id_(0),
      needs_handshake_ack_(false),
      successive_handshake_retries_(0),
      next_scheduled_handshake_update_(roo_time::Uptime::Start()),
      sender_thread_() {
  CHECK_LE(sendbuf_log2, 12);
  CHECK_LE(sendbuf_log2, recvbuf_log2);
  receiver.setReceiverFn(
      [this](const roo::byte* buf, size_t len) { packetReceived(buf, len); });
  // while (my_stream_id_ == 0) my_stream_id_ = rand();
}

size_t Channel::write(const roo::byte* buf, size_t count, uint32_t my_stream_id,
                      roo_io::Status& stream_status) {
  return transmitter_.write(buf, count, my_stream_id, stream_status);
}

size_t Channel::tryWrite(const roo::byte* buf, size_t count,
                         uint32_t my_stream_id, roo_io::Status& stream_status) {
  return transmitter_.tryWrite(buf, count, my_stream_id, stream_status);
}

size_t Channel::read(roo::byte* buf, size_t count, uint32_t my_stream_id,
                     roo_io::Status& stream_status) {
  return receiver_.read(buf, count, my_stream_id, stream_status);
}

size_t Channel::tryRead(roo::byte* buf, size_t count, uint32_t my_stream_id,
                        roo_io::Status& stream_status) {
  return receiver_.tryRead(buf, count, my_stream_id, stream_status);
}

int Channel::peek(uint32_t my_stream_id, roo_io::Status& stream_status) {
  return receiver_.peek(my_stream_id, stream_status);
}

size_t Channel::availableForRead(uint32_t my_stream_id,
                                 roo_io::Status& stream_status) const {
  return receiver_.availableForRead(my_stream_id, stream_status);
}

void Channel::flush(uint32_t my_stream_id, roo_io::Status& stream_status) {
  transmitter_.flush(my_stream_id, stream_status);
}

void Channel::close(uint32_t my_stream_id, roo_io::Status& stream_status) {
  transmitter_.close(my_stream_id, stream_status);
}

void Channel::closeInput(uint32_t my_stream_id, roo_io::Status& stream_status) {
  receiver_.markInputClosed(my_stream_id, stream_status);
}

void Channel::onReceive(internal::ThreadSafeReceiver::RecvCb recv_cb,
                        uint32_t my_stream_id, roo_io::Status& stream_status) {
  receiver_.onReceive(recv_cb, my_stream_id, stream_status);
}

size_t Channel::availableForWrite(uint32_t my_stream_id,
                                  roo_io::Status& stream_status) const {
  return transmitter_.availableForWrite(my_stream_id, stream_status);
}

uint32_t Channel::my_stream_id() const {
  roo::lock_guard<roo::mutex> guard(handshake_mutex_);
  return my_stream_id_;
}

uint32_t Channel::connect() {
  roo::lock_guard<roo::mutex> guard(handshake_mutex_);
  my_stream_id_ = 0;
  peer_stream_id_ = 0;
  // The stream ID is a random number, but it can't be zero.
  while (my_stream_id_ == 0) my_stream_id_ = rand();
  transmitter_.init(my_stream_id_, rand() % 0x0FFF);
  receiver_.init(my_stream_id_);
  needs_handshake_ack_ = false;
  successive_handshake_retries_ = 0;
  next_scheduled_handshake_update_ = roo_time::Uptime::Start();
  // We need to send that handshake message.
  outgoing_data_ready_.notify();
  connected_cv_.notify_all();
  return my_stream_id_;
}

bool Channel::isConnecting(uint32_t stream_id) {
  roo::lock_guard<roo::mutex> guard(handshake_mutex_);
  return my_stream_id_ == stream_id && peer_stream_id_ == 0;
}

void Channel::awaitConnected(uint32_t stream_id) {
  roo::unique_lock<roo::mutex> guard(handshake_mutex_);
  while (isConnecting(stream_id)) {
    connected_cv_.wait(guard);
  }
}

bool Channel::awaitConnected(uint32_t stream_id, roo_time::Interval timeout) {
  roo::unique_lock<roo::mutex> guard(handshake_mutex_);
  roo_time::Uptime when = roo_time::Uptime::Now() + timeout;
  while (isConnecting(stream_id)) {
    if (connected_cv_.wait_until(guard, when) == roo::cv_status::timeout) {
      return false;
    }
  }
  return true;
}

bool Channel::loop() {
#ifndef ESP32
  tryRecv();
#endif
  // long delay = trySend();
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

bool Channel::tryRecv() { return packet_receiver_.tryReceive(); }

size_t Channel::conn(roo::byte* buf, long& next_send_micros) {
#ifdef ROO_USE_THREADS
  roo::lock_guard<roo::mutex> guard(handshake_mutex_);
#endif
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
  uint16_t header =
      FormatPacketHeader(transmitter_.front(), internal::kHandshakePacket);
  roo_io::StoreBeU16(header, buf);
  roo_io::StoreBeU32(my_stream_id_, buf + 2);
  roo_io::StoreBeU32(peer_stream_id_, buf + 6);
  roo_io::StoreU8(
      transmitter_state == internal::Transmitter::kConnected ? 0x0 : 0xFF,
      buf + 10);
  next_send_micros = std::min(next_send_micros, delay);
  // LOG(INFO) << "Handshake packet sent: peer_seq_num="
  //           << (header & 0x0FFF) << ", my_stream_id=" << my_stream_id_
  //           << ", peer_stream_id=" << peer_stream_id_
  //           << ", want_ack=" << (transmitter_state !=
  //                               internal::Transmitter::kConnected);
  return 11;
}

void Channel::handleHandshakePacket(uint16_t peer_seq_num,
                                    uint32_t peer_stream_id,
                                    uint32_t ack_stream_id, bool want_ack) {
#ifdef ROO_USE_THREADS
  roo::lock_guard<roo::mutex> guard(handshake_mutex_);
#endif
  // LOG(INFO) << "Handshake packet received: peer_seq_num="
  //            << peer_seq_num << ", peer_stream_id=" << peer_stream_id
  //            << ", ack_stream_id=" << ack_stream_id
  //            << ", want_ack=" << want_ack;
  switch (receiver_.state()) {
    case internal::Receiver::kConnecting: {
      peer_stream_id_ = peer_stream_id;
      CHECK(receiver_.empty());
      receiver_.setConnected(peer_seq_num);

      if (ack_stream_id == my_stream_id_) {
        transmitter_.setConnected();
      }
      needs_handshake_ack_ = want_ack;
      connected_cv_.notify_all();
      break;
    }
    case internal::Receiver::kConnected: {
      if ((peer_stream_id_ != peer_stream_id) ||
          ((ack_stream_id != my_stream_id_ &&
            transmitter_.state() == internal::Transmitter::kConnected))) {
        // The peer opened a new stream.
        if (!receiver_.done()) {
          // LOG(WARNING) << "Disconnection detected: " << peer_stream_id_ << ",
          // "
          //              << peer_stream_id;
          // Ignore until all in-flight packets have been delivered.
          if (transmitter_.state() == internal::Transmitter::kConnected) {
            transmitter_.setBroken();
          } else {
            transmitter_.reset();
            my_stream_id_ = 0;
          }
          connected_cv_.notify_all();
          receiver_.setBroken();
          break;
        }
        transmitter_.reset();
        my_stream_id_ = 0;
        receiver_.reset();
        connected_cv_.notify_all();
        break;
      }
      if (ack_stream_id == my_stream_id_) {
        CHECK(my_stream_id_ != 0);
        transmitter_.setConnected();
      }
      needs_handshake_ack_ = want_ack;
      break;
    }
    case internal::Receiver::kIdle:
    case internal::Receiver::kBroken: {
      // We're idle; ignoring handshake;
      break;
    }
    default: {
      break;
    }
  }
}

void Channel::packetReceived(const roo::byte* buf, size_t len) {
  uint16_t header = roo_io::LoadBeU16(buf);
  auto type = internal::GetPacketType(header);
  switch (type) {
    case internal::kDataAckPacket: {
      transmitter_.ack(header & 0x0FFF, buf + 2, len - 2);
      return;
    }
    case internal::kFlowControlPacket: {
      // Update to available slots received.
      transmitter_.updateRecvHimark(header & 0x0FFF);
      return;
    }
    case internal::kHandshakePacket: {
      if (len != 11) {
        // Malformed packet.
        return;
      }
      uint16_t peer_seq_num = header & 0x0FFF;
      uint32_t peer_stream_id = roo_io::LoadBeU32(buf + 2);
      uint32_t ack_stream_id = roo_io::LoadBeU32(buf + 6);
      bool want_ack = roo_io::LoadU8(buf + 10) != 0;
      handleHandshakePacket(peer_seq_num, peer_stream_id, ack_stream_id,
                            want_ack);
      if (want_ack) {
        outgoing_data_ready_.notify();
      }
      return;
    }
    case internal::kDataPacket:
    case internal::kFinPacket: {
      if (receiver_.handleDataPacket(header & 0x0FFF, buf + 2, len - 2,
                                     type == internal::kFinPacket)) {
        outgoing_data_ready_.notify();
      }
    }
    default: {
      // Unrecognized packet type; ignoring.
    }
  }
}

// namespace {

void SendLoop(Channel* retransmitter) {
  while (true) {
    long delay_micros = retransmitter->trySend();
    yield();
    if (delay_micros > 0) {
      // Wait for the delay, or the notification that we have data to send,
      // whichever comes first.
      retransmitter->outgoing_data_ready_.await(delay_micros);
    }
  }
}

void MySendLoop(void* pvParameters) { SendLoop((Channel*)pvParameters); }

void Channel::begin() {
  TaskHandle_t xHandle = NULL;
  // xTaskCreatePinnedToCore(&MySendLoop, "send_loop", 4096, this, 1,  //
  // tskIDLE_PRIORITY,
  //   &xHandle, 0);

  xTaskCreate(&MySendLoop, "send_loop", 8192, this, 1,  // tskIDLE_PRIORITY,
              &xHandle);

  // sender_thread_ = roo::thread(SendLoop, this);
}

}  // namespace roo_transport

#endif  // ROO_USE_THREADS
