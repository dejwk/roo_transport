#pragma once

#include "roo_transport/link/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_io/status.h"
#include "roo_transport/link/internal/receiver.h"
#include "roo_transport/link/internal/thread_safe/outgoing_data_ready_notification.h"

namespace roo_transport {
namespace internal {

class ThreadSafeReceiver {
 public:
  // Can be supplied to be notified when new data is available for read.
  using RecvCb = std::function<void()>;

  ThreadSafeReceiver(unsigned int recvbuf_log2,
                     OutgoingDataReadyNotification& notification);

  Receiver::State state() const;

  void setConnected(SeqNum peer_seq_num);
  void setBroken();

  size_t read(roo::byte* buf, size_t count, uint32_t my_stream_id,
              roo_io::Status& stream_status);

  size_t tryRead(roo::byte* buf, size_t count, uint32_t my_stream_id,
                 roo_io::Status& stream_status);

  int peek(uint32_t my_stream_id, roo_io::Status& stream_status);

  size_t availableForRead(uint32_t my_stream_id,
                          roo_io::Status& stream_status) const;

  void markInputClosed(uint32_t my_stream_id, roo_io::Status& stream_status);

  void reset();
  void init(uint32_t my_stream_id);

  size_t ack(roo::byte* buf);
  size_t updateRecvHimark(roo::byte* buf, long& next_send_micros);

  bool handleDataPacket(uint16_t seq_id, const roo::byte* payload, size_t len,
                        bool is_final);

  bool empty() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return receiver_.empty();
  }

  bool done() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return receiver_.done();
  }

  uint32_t packets_received() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return receiver_.packets_received();
  }

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
  OutgoingDataReadyNotification& outgoing_data_ready_;
};

}  // namespace internal
}  // namespace roo_transport

#endif  // ROO_USE_THREADS
