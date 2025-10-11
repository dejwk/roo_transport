#pragma once

#include "roo_transport/link/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_io/core/input_stream.h"
#include "roo_transport/link/internal/thread_safe/channel.h"

namespace roo_transport {

class LinkInputStream : public roo_io::InputStream {
 public:
  LinkInputStream()
      : channel_(nullptr), my_stream_id_(0), status_(roo_io::kClosed) {}

  LinkInputStream(Channel& channel, uint32_t my_stream_id)
      : channel_(&channel),
        my_stream_id_(my_stream_id),
        status_(my_stream_id == 0 ? roo_io::kClosed : roo_io::kOk) {}

  LinkInputStream(const LinkInputStream&) = delete;
  LinkInputStream& operator=(const LinkInputStream&) = delete;

  LinkInputStream(LinkInputStream&& other);
  LinkInputStream& operator=(LinkInputStream&& other);

  void close() override;

  size_t read(roo::byte* buf, size_t count) override;

  size_t tryRead(roo::byte* buf, size_t count) override;

  size_t available();
  int read();
  int peek();

  roo_io::Status status() const override { return status_; }

 private:
  Channel* channel_;
  uint32_t my_stream_id_;
  roo_io::Status status_;
};

}  // namespace roo_transport

#endif  // ROO_USE_THREADS