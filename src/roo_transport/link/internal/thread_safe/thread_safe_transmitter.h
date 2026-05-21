#pragma once

#include "roo_transport/link/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_io/status.h"
#include "roo_threads.h"
#include "roo_threads/mutex.h"
#include "roo_transport/link/internal/transmitter.h"

namespace roo_transport {
namespace internal {

/// Thread-safe wrapper around `Transmitter`.
class ThreadSafeTransmitter {
 public:
  /// Creates a transmitter with a send buffer of size `1 << sendbuf_log2`.
  ThreadSafeTransmitter(unsigned int sendbuf_log2);

  /// Resets the transmitter to the idle state.
  void reset();

  /// Initializes a new outgoing stream.
  void init(uint32_t my_stream_id, SeqNum new_start);

  /// Returns the number of sent packets, including retransmissions.
  uint32_t packets_sent() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return transmitter_.packets_sent();
  }

  /// Returns the number of packets confirmed as delivered.
  uint32_t packets_delivered() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return transmitter_.packets_delivered();
  }

  /// Writes up to `count` bytes, blocking if needed.
  size_t write(const roo::byte* buf, size_t count, uint32_t my_stream_id,
               roo_io::Status& stream_status, bool& outgoing_data_ready);

  /// Writes up to `count` bytes without blocking.
  size_t tryWrite(const roo::byte* buf, size_t count, uint32_t my_stream_id,
                  roo_io::Status& stream_status, bool& outgoing_data_ready);

  /// Returns the guaranteed writable byte count.
  size_t availableForWrite(uint32_t my_stream_id,
                           roo_io::Status& stream_status) const;

  /// Flushes pending buffered output.
  void flush(uint32_t my_stream_id, roo_io::Status& stream_status,
             bool& outgoing_data_ready);

  /// Returns whether there is still unacknowledged data pending.
  bool hasPendingData(uint32_t my_stream_id,
                      roo_io::Status& stream_status) const;

  /// Closes the stream after all buffered data is acknowledged.
  void close(uint32_t my_stream_id, roo_io::Status& stream_status,
             bool& outgoing_data_ready);

  /// Marks the transmitter connected to the peer.
  void setConnected(uint16_t peer_receive_buffer_size, bool control_bit) {
    roo::lock_guard<roo::mutex> guard(mutex_);
    transmitter_.setConnected(peer_receive_buffer_size, control_bit);
  }

  /// Marks the transmitter broken and wakes blocked writers.
  void setBroken() {
    roo::lock_guard<roo::mutex> guard(mutex_);
    transmitter_.setBroken();
    all_acked_.notify_all();
    has_space_.notify_all();
  }

  /// Returns the current transmitter state.
  Transmitter::State state() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return transmitter_.state();
  }

  /// Serializes the next packet to send into `buf`.
  size_t send(roo::byte* buf, long& next_send_micros);

  /// Returns the first unacknowledged sequence number.
  SeqNum front() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return transmitter_.front();
  }

  /// Applies an acknowledgment bitmap from the peer.
  void ack(bool control_bit, uint16_t seq_id, const roo::byte* ack_bitmap,
           size_t ack_bitmap_len, bool& outgoing_data_ready);

  /// Updates the peer receive high-water mark.
  void updateRecvHimark(bool control_bit, uint16_t recv_himark) {
    roo::lock_guard<roo::mutex> guard(mutex_);
    if (transmitter_.updateRecvHimark(control_bit, recv_himark)) {
      has_space_.notify_all();
    }
  }

 private:
  // Checks the state of the underlying receiver, and whether its stream ID
  // matches my_stream_id. If there is no match, it means that the connection
  // has been interrupted. This method sets
  // status accordingly, to either kOk (if match), or kConnectionError (if
  // mismatch). It returns true when status is kOk; false otherwise.
  //
  // Must be called with mutex_ held.
  bool checkConnectionStatus(uint32_t my_stream_id,
                             roo_io::Status& status) const;

  internal::Transmitter transmitter_;

  mutable roo::mutex mutex_;

  // Notifies the application writer thread that the output stream might have
  // some space for writing new data.
  roo::condition_variable has_space_;

  // Notifies the application writer thread that the send buffer has been
  // entirely acked (all data has been delivered).
  roo::condition_variable all_acked_;
};

}  // namespace internal
}  // namespace roo_transport

#endif  // ROO_USE_THREADS