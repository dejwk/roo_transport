#include "roo_transport/link/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_transport/link/internal/thread_safe/thread_safe_transmitter.h"

namespace roo_transport {
namespace internal {

ThreadSafeTransmitter::ThreadSafeTransmitter(unsigned int sendbuf_log2)
    : transmitter_(sendbuf_log2) {}

bool ThreadSafeTransmitter::checkConnectionStatus(
    uint32_t my_stream_id, roo_io::Status& status) const {
  if ((transmitter_.state() == Transmitter::kConnected ||
       transmitter_.state() == Transmitter::kConnecting) &&
      my_stream_id == transmitter_.my_stream_id()) {
    status = roo_io::kOk;
    return true;
  }
  status = roo_io::kConnectionError;
  return false;
}

size_t ThreadSafeTransmitter::write(const roo::byte* buf, size_t count,
                                    uint32_t my_stream_id,
                                    roo_io::Status& stream_status,
                                    bool& outgoing_data_ready) {
  roo::unique_lock<roo::mutex> guard(mutex_);
  if (!checkConnectionStatus(my_stream_id, stream_status)) return 0;
  while (true) {
    size_t total_written =
        transmitter_.tryWrite(buf, count, outgoing_data_ready);
    if (total_written > 0) {
      return total_written;
    }
    if (!checkConnectionStatus(my_stream_id, stream_status)) return 0;
    // Wait for space to be available.
    has_space_.wait(guard);
  }
}

size_t ThreadSafeTransmitter::tryWrite(const roo::byte* buf, size_t count,
                                       uint32_t my_stream_id,
                                       roo_io::Status& stream_status,
                                       bool& outgoing_data_ready) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  if (!checkConnectionStatus(my_stream_id, stream_status)) return 0;
  size_t total_written = transmitter_.tryWrite(buf, count, outgoing_data_ready);
  return total_written;
}

void ThreadSafeTransmitter::flush(uint32_t my_stream_id,
                                  roo_io::Status& stream_status,
                                  bool& outgoing_data_ready) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  if (!checkConnectionStatus(my_stream_id, stream_status)) return;
  if (transmitter_.flush()) {
    outgoing_data_ready = true;
  }
}

bool ThreadSafeTransmitter::hasPendingData(
    uint32_t my_stream_id, roo_io::Status& stream_status) const {
  if (!checkConnectionStatus(my_stream_id, stream_status)) return false;
  roo::lock_guard<roo::mutex> guard(mutex_);
  return transmitter_.hasPendingData();
}

void ThreadSafeTransmitter::close(uint32_t my_stream_id,
                                  roo_io::Status& stream_status,
                                  bool& outgoing_data_ready) {
  roo::unique_lock<roo::mutex> guard(mutex_);
  if (!checkConnectionStatus(my_stream_id, stream_status)) return;
  transmitter_.close();
  outgoing_data_ready = true;
  if (!transmitter_.hasPendingData()) return;
  while (true) {
    all_acked_.wait(guard);
    if (!transmitter_.hasPendingData()) return;
    if (!checkConnectionStatus(my_stream_id, stream_status)) return;
  }
}

size_t ThreadSafeTransmitter::availableForWrite(
    uint32_t my_stream_id, roo_io::Status& stream_status) const {
  roo::lock_guard<roo::mutex> guard(mutex_);
  if (!checkConnectionStatus(my_stream_id, stream_status)) return 0;
  return transmitter_.availableForWrite();
}

size_t ThreadSafeTransmitter::send(roo::byte* buf, long& next_send_micros) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  const internal::OutBuffer* buf_to_send =
      transmitter_.getBufferToSend(next_send_micros);
  if (buf_to_send == nullptr) return 0;

  const roo::byte* data = buf_to_send->data();
  size_t size = buf_to_send->size();
  memcpy(buf, data, size);
  return size;
}

void ThreadSafeTransmitter::reset() {
  roo::lock_guard<roo::mutex> guard(mutex_);
  transmitter_.reset();
  all_acked_.notify_all();
}

void ThreadSafeTransmitter::init(uint32_t my_stream_id, SeqNum new_start) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  transmitter_.init(my_stream_id, new_start);
  all_acked_.notify_all();
}

void ThreadSafeTransmitter::ack(bool control_bit, uint16_t seq_id,
                                const roo::byte* ack_bitmap,
                                size_t ack_bitmap_len,
                                bool& outgoing_data_ready) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  if (transmitter_.ack(control_bit, seq_id, ack_bitmap, ack_bitmap_len)) {
    // We have a new packet ready to be sent.
    outgoing_data_ready = true;
  }
  if (!transmitter_.hasPendingData()) {
    all_acked_.notify_all();
  }
}

}  // namespace internal
}  // namespace roo_transport

#endif  // ROO_USE_THREADS