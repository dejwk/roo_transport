#pragma once

#include "roo_transport/link/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_io/status.h"
#include "roo_threads.h"
#include "roo_threads/mutex.h"
#include "roo_transport/link/internal/transmitter.h"

namespace roo_transport {
namespace internal {

class ThreadSafeTransmitter {
 public:
  ThreadSafeTransmitter(unsigned int sendbuf_log2);

  void reset();

  void init(uint32_t my_stream_id, SeqNum new_start);

  uint32_t packets_sent() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return transmitter_.packets_sent();
  }

  uint32_t packets_delivered() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return transmitter_.packets_delivered();
  }

  size_t write(const roo::byte* buf, size_t count, uint32_t my_stream_id,
               roo_io::Status& stream_status, bool& outgoing_data_ready);

  size_t tryWrite(const roo::byte* buf, size_t count, uint32_t my_stream_id,
                  roo_io::Status& stream_status, bool& outgoing_data_ready);

  size_t availableForWrite(uint32_t my_stream_id,
                           roo_io::Status& stream_status) const;

  void flush(uint32_t my_stream_id, roo_io::Status& stream_status,
             bool& outgoing_data_ready);

  bool hasPendingData(uint32_t my_stream_id,
                      roo_io::Status& stream_status) const;

  // Closes the stream and blocks until all the data has been confirmed by the
  // recipient.
  void close(uint32_t my_stream_id, roo_io::Status& stream_status,
             bool& outgoing_data_ready);

  void setConnected() {
    roo::lock_guard<roo::mutex> guard(mutex_);
    transmitter_.setConnected();
  }

  void setBroken() {
    roo::lock_guard<roo::mutex> guard(mutex_);
    transmitter_.setBroken();
    all_acked_.notify_all();
    has_space_.notify_all();
  }

  Transmitter::State state() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return transmitter_.state();
  }

  size_t send(roo::byte* buf, long& next_send_micros);

  SeqNum front() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return transmitter_.front();
  }

  void ack(uint16_t seq_id, const roo::byte* ack_bitmap, size_t ack_bitmap_len,
           bool& outgoing_data_ready);

  void updateRecvHimark(uint16_t recv_himark) {
    roo::lock_guard<roo::mutex> guard(mutex_);
    if (transmitter_.updateRecvHimark(recv_himark)) {
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