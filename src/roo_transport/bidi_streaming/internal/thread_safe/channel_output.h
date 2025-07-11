#pragma once

#include "roo_transport/bidi_streaming/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_io/core/input_stream.h"
#include "roo_transport/bidi_streaming/internal/thread_safe/channel.h"

namespace roo_io {

class ChannelOutput : public roo_io::OutputStream {
 public:
  ChannelOutput() : channel_(nullptr), my_stream_id_(0), status_(kClosed) {}

  ChannelOutput(Channel& channel, uint32_t my_stream_id)
      : channel_(&channel), my_stream_id_(my_stream_id), status_(kOk) {}

  size_t write(const roo::byte* buf, size_t count) override;

  size_t tryWrite(const roo::byte* buf, size_t count) override;

  int availableForWrite();

  void flush() override;

  void close() override;

  roo_io::Status status() const override { return status_; }

 private:
  Channel* channel_;
  uint32_t my_stream_id_;
  roo_io::Status status_;
};

}  // namespace roo_io

#endif  // ROO_USE_THREADS