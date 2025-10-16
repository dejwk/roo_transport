#include "roo_transport/link/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_transport/link/internal/thread_safe/thread_safe_receiver.h"

namespace roo_transport {
namespace internal {

ThreadSafeReceiver::ThreadSafeReceiver(unsigned int recvbuf_log2)
    : receiver_(recvbuf_log2) {}

Receiver::State ThreadSafeReceiver::state() const {
  roo::lock_guard<roo::mutex> guard(mutex_);
  return receiver_.state();
}

void ThreadSafeReceiver::setConnected(SeqNum peer_seq_num) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  receiver_.setConnected(peer_seq_num);
}

void ThreadSafeReceiver::setBroken() {
  roo::lock_guard<roo::mutex> guard(mutex_);
  receiver_.setBroken();
  has_data_.notify_all();
}

bool ThreadSafeReceiver::checkConnectionStatus(uint32_t my_stream_id,
                                               roo_io::Status& status) const {
  if (my_stream_id != receiver_.my_stream_id()) {
    // Peer reconnected with a new ID, and we already accepted. This connection
    // is dead.
    status = roo_io::kConnectionError;
    return false;
  }
  if (receiver_.state() == Receiver::kIdle) {
    // Peer reconnected with a new ID. We have not accepted yet, but this
    // connection is dead.
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
                                roo_io::Status& stream_status,
                                bool& outgoing_data_ready) {
  if (count == 0) return 0;
  roo::unique_lock<roo::mutex> guard(mutex_);
  if (!checkConnectionStatus(my_stream_id, stream_status)) return 0;
  while (true) {
    size_t total_read = receiver_.tryRead(buf, count, outgoing_data_ready);
    if (total_read > 0) {
      return total_read;
    }
    if (!checkConnectionStatus(my_stream_id, stream_status)) return 0;
    has_data_.wait(guard);
  }
}

size_t ThreadSafeReceiver::tryRead(roo::byte* buf, size_t count,
                                   uint32_t my_stream_id,
                                   roo_io::Status& stream_status,
                                   bool& outgoing_data_ready) {
  if (count == 0) return 0;
  roo::lock_guard<roo::mutex> guard(mutex_);
  if (!checkConnectionStatus(my_stream_id, stream_status)) return 0;
  size_t total_read = receiver_.tryRead(buf, count, outgoing_data_ready);
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
                                         roo_io::Status& stream_status,
                                         bool& outgoing_data_ready) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  if (!checkConnectionStatus(my_stream_id, stream_status)) return;
  receiver_.markInputClosed(outgoing_data_ready);
}

void ThreadSafeReceiver::reset() {
  roo::lock_guard<roo::mutex> guard(mutex_);
  receiver_.reset();
  has_data_.notify_all();
}

void ThreadSafeReceiver::init(uint32_t my_stream_id) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  receiver_.init(my_stream_id);
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
  roo::lock_guard<roo::mutex> guard(mutex_);
  has_ack_to_send = receiver_.handleDataPacket(seq_id, payload, len, is_final,
                                               has_new_data_to_read);
  if (has_new_data_to_read) {
    has_data_.notify_all();
  }
  return has_ack_to_send;
}

unsigned int ThreadSafeReceiver::buffer_size_log2() const {
  roo::lock_guard<roo::mutex> guard(mutex_);
  return receiver_.buffer_size_log2();
}

}  // namespace internal
}  // namespace roo_transport

#endif  // ROO_USE_THREADS
