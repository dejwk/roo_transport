#include "roo_transport/singleton_socket/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_transport/singleton_socket/internal/thread_safe/thread_safe_receiver.h"

namespace roo_transport {
namespace internal {

ThreadSafeReceiver::ThreadSafeReceiver(
    unsigned int recvbuf_log2,
    internal::OutgoingDataReadyNotification& outgoing_data_ready)
    : receiver_(recvbuf_log2),
      outgoing_data_ready_(outgoing_data_ready),
      recv_cb_(nullptr),
      recv_cb_stream_id_(0) {}

Receiver::State ThreadSafeReceiver::state() const {
  roo::lock_guard<roo::mutex> guard(mutex_);
  return receiver_.state();
}

void ThreadSafeReceiver::setConnected(SeqNum peer_seq_num) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  receiver_.setConnected(peer_seq_num);
  outgoing_data_ready_.notify();
}

void ThreadSafeReceiver::setIdle() {
  roo::lock_guard<roo::mutex> guard(mutex_);
  receiver_.setIdle();
  has_data_.notify_all();
}

void ThreadSafeReceiver::setBroken() {
  roo::lock_guard<roo::mutex> guard(mutex_);
  receiver_.setBroken();
  has_data_.notify_all();
}

bool ThreadSafeReceiver::checkConnectionStatus(uint32_t my_stream_id,
                                               roo_io::Status& status) const {
  if (my_stream_id != receiver_.my_stream_id()) {
    status = roo_io::kConnectionError;
    return false;
  }
  if (receiver_.state() == Receiver::kIdle) {
    status = roo_io::kConnectionError;
    return false;
  }
  if (receiver_.eos()) {
    status = roo_io::kEndOfStream;
    return false;
  }
  status = roo_io::kOk;
  return true;
}

size_t ThreadSafeReceiver::read(roo::byte* buf, size_t count,
                                uint32_t my_stream_id,
                                roo_io::Status& stream_status) {
  if (count == 0) return 0;
  roo::unique_lock<roo::mutex> guard(mutex_);
  if (!checkConnectionStatus(my_stream_id, stream_status)) return 0;
  while (true) {
    bool outgoing_data_ready = false;
    size_t total_read = receiver_.tryRead(buf, count, outgoing_data_ready);
    if (outgoing_data_ready) {
      outgoing_data_ready_.notify();
    }
    if (total_read > 0) {
      return total_read;
    }
    if (!checkConnectionStatus(my_stream_id, stream_status)) return 0;
    has_data_.wait(guard);
  }
}

size_t ThreadSafeReceiver::tryRead(roo::byte* buf, size_t count,
                                   uint32_t my_stream_id,
                                   roo_io::Status& stream_status) {
  if (count == 0) return 0;
  roo::lock_guard<roo::mutex> guard(mutex_);
  if (!checkConnectionStatus(my_stream_id, stream_status)) return 0;
  bool outgoing_data_ready = false;
  size_t total_read = receiver_.tryRead(buf, count, outgoing_data_ready);
  if (outgoing_data_ready) {
    outgoing_data_ready_.notify();
  }
  return total_read;
}

int ThreadSafeReceiver::peek(uint32_t my_stream_id,
                             roo_io::Status& stream_status) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  if (!checkConnectionStatus(my_stream_id, stream_status)) return -1;
  return receiver_.peek();
}

size_t ThreadSafeReceiver::availableForRead(
    uint32_t my_stream_id, roo_io::Status& stream_status) const {
  roo::lock_guard<roo::mutex> guard(mutex_);
  if (!checkConnectionStatus(my_stream_id, stream_status)) return 0;
  return receiver_.availableForRead();
}

void ThreadSafeReceiver::markInputClosed(uint32_t my_stream_id,
                                         roo_io::Status& stream_status) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  if (!checkConnectionStatus(my_stream_id, stream_status)) return;
  bool outgoing_data_ready = false;
  receiver_.markInputClosed(outgoing_data_ready);
  if (outgoing_data_ready) {
    outgoing_data_ready_.notify();
  }
}

void ThreadSafeReceiver::onReceive(RecvCb recv_cb, uint32_t my_stream_id,
                                   roo_io::Status& stream_status) {
  if (!checkConnectionStatus(my_stream_id, stream_status)) return;
  recv_cb_ = recv_cb;
  recv_cb_stream_id_ = my_stream_id;
}

void ThreadSafeReceiver::reset() {
  roo::lock_guard<roo::mutex> guard(mutex_);
  receiver_.reset();
  has_data_.notify_all();
  recv_cb_ = nullptr;
  recv_cb_stream_id_ = 0;
}

void ThreadSafeReceiver::init(uint32_t my_stream_id) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  receiver_.init(my_stream_id);
  // Need to init the handshake.
  outgoing_data_ready_.notify();
  has_data_.notify_all();
}

size_t ThreadSafeReceiver::ack(roo::byte* buf) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  return receiver_.ack(buf);
}

size_t ThreadSafeReceiver::updateRecvHimark(roo::byte* buf,
                                            long& next_send_micros) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  return receiver_.updateRecvHimark(buf, next_send_micros);
}

bool ThreadSafeReceiver::handleDataPacket(uint16_t seq_id,
                                          const roo::byte* payload, size_t len,
                                          bool is_final) {
  bool has_new_data_to_read = false;
  bool has_ack_to_send;
  RecvCb recv_cb_to_call = nullptr;
  {
    roo::lock_guard<roo::mutex> guard(mutex_);
    has_ack_to_send = receiver_.handleDataPacket(seq_id, payload, len, is_final,
                                                 has_new_data_to_read);
    if (has_new_data_to_read) {
      has_data_.notify_all();
    }
    if (has_new_data_to_read &&
        recv_cb_stream_id_ == receiver_.my_stream_id()) {
      recv_cb_to_call = recv_cb_;
    }
  }
  if (recv_cb_to_call != nullptr) {
    recv_cb_to_call();
  }
  return has_ack_to_send;
}

}  // namespace internal
}  // namespace roo_transport

#endif  // ROO_USE_THREADS
