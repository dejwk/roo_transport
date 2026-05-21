#pragma once

#include "roo_transport/link/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_io/status.h"
#include "roo_transport/link/internal/receiver.h"
#include "roo_transport/link/internal/thread_safe/outgoing_data_ready_notification.h"

namespace roo_transport {
namespace internal {

/// Thread-safe wrapper around `Receiver`.
class ThreadSafeReceiver {
 public:
  /// Callback invoked when new input data becomes available.
  using RecvCb = std::function<void()>;

  /// Creates a receiver with a buffer of size `1 << recvbuf_log2`.
  ThreadSafeReceiver(unsigned int recvbuf_log2);

  /// Returns the current receiver state.
  Receiver::State state() const;

  /// Marks the receiver connected to the peer.
  void setConnected(SeqNum peer_seq_num, bool control_bit);
  /// Marks the receiver broken.
  void setBroken();

  /// Reads up to `count` bytes, blocking if needed.
  size_t read(roo::byte* buf, size_t count, uint32_t my_stream_id,
              roo_io::Status& stream_status, bool& outgoing_data_ready);

  /// Reads up to `count` bytes without blocking.
  size_t tryRead(roo::byte* buf, size_t count, uint32_t my_stream_id,
                 roo_io::Status& stream_status, bool& outgoing_data_ready);

  /// Peeks at the next byte without consuming it.
  int peek(uint32_t my_stream_id, roo_io::Status& stream_status);

  /// Returns bytes currently available for immediate reading.
  size_t availableForRead(uint32_t my_stream_id,
                          roo_io::Status& stream_status) const;

  /// Closes the local input side of the stream.
  void markInputClosed(uint32_t my_stream_id, roo_io::Status& stream_status,
                       bool& outgoing_data_ready);

  /// Resets the receiver to the idle state.
  void reset();
  /// Initializes a new incoming stream.
  void init(uint32_t my_stream_id);

  /// Serializes an acknowledgment packet into `buf`.
  size_t ack(roo::byte* buf);
  /// Serializes a flow-control update into `buf`.
  size_t updateRecvHimark(roo::byte* buf, long& next_send_micros);

  /// Handles one received data packet.
  bool handleDataPacket(bool control_bit, uint16_t seq_id,
                        const roo::byte* payload, size_t len, bool is_final);

  /// Returns whether there is no buffered input.
  bool empty() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return receiver_.empty();
  }

  /// Returns whether the stream has reached end-of-stream.
  bool done() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return receiver_.done();
  }

  /// Returns the number of packets received.
  uint32_t packets_received() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return receiver_.packets_received();
  }

  /// Returns receive buffer capacity as a log2 value.
  unsigned int buffer_size_log2() const;

 private:
  // Checks the state of the underlying receiver, and whether its stream ID
  // matches my_stream_id. If there is no match, it means that the connection
  // has been interrupted. If there is a match but the receiver is in the
  // 'closed' state, it means that EOF has been encountered. This method sets
  // status accordingly, to either kOk (if match and not closed), kEndOfStream
  // (if match and closed), or kConnectionError (if mismatch). It returns true
  // when status is kOk; false otherwise.
  //
  // Must be called with mutex_ held.
  bool checkConnectionStatus(uint32_t my_stream_id,
                             roo_io::Status& status) const;

  internal::Receiver receiver_;

  mutable roo::mutex mutex_;
  roo::condition_variable has_data_;
};

}  // namespace internal
}  // namespace roo_transport

#endif  // ROO_USE_THREADS
