#pragma once

#include "roo_transport/link/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_io/core/input_stream.h"
#include "roo_transport/link/internal/thread_safe/channel.h"
#include "roo_transport/socket_input_stream.h"

namespace roo_transport {

class LinkInputStream : public roo_transport::SocketInputStream {
 public:
  LinkInputStream()
      : channel_(nullptr), my_stream_id_(0), status_(roo_io::kClosed) {}

  LinkInputStream(Channel& channel, uint32_t my_stream_id)
      : channel_(&channel),
        my_stream_id_(my_stream_id),
        status_(my_stream_id == 0 ? roo_io::kClosed : roo_io::kOk) {}

  void close() override;

  size_t read(roo::byte* buf, size_t count) override;

  size_t tryRead(roo::byte* buf, size_t count) override;

  size_t available() override;
  int read() override;
  int peek() override;

  roo_io::Status status() const override { return status_; }

 private:
  Channel* channel_;
  uint32_t my_stream_id_;
  roo_io::Status status_;
};

}  // namespace roo_transport

#endif  // ROO_USE_THREADS