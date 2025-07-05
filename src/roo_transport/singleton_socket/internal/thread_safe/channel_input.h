#pragma once

#include "roo_transport/singleton_socket/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_io/core/input_stream.h"
#include "roo_transport/singleton_socket/internal/thread_safe/channel.h"

namespace roo_io {

class ChannelInput : public roo_io::InputStream {
 public:
  ChannelInput() : channel_(nullptr), my_stream_id_(0), status_(kClosed) {}

  ChannelInput(Channel& channel, uint32_t my_stream_id)
      : channel_(&channel),
        my_stream_id_(my_stream_id),
        status_(my_stream_id == 0 ? kClosed : kOk) {}

  void close() override;

  size_t read(roo::byte* buf, size_t count) override;

  size_t tryRead(roo::byte* buf, size_t count) override;

  void onReceive(internal::ThreadSafeReceiver::RecvCb recv_cb);

  int available();
  int read();
  int peek();

  size_t timedRead(roo::byte* buf, size_t count, roo_time::Interval timeout);

  roo_io::Status status() const override { return status_; }

 private:
  Channel* channel_;
  uint32_t my_stream_id_;
  roo_io::Status status_;
};

}  // namespace roo_io

#endif  // ROO_USE_THREADS