#pragma once

#include "roo_transport/link/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_io/core/input_stream.h"
#include "roo_transport/link/internal/thread_safe/channel.h"

namespace roo_transport {

/// Output stream view over the outbound side of a `Link`.
class LinkOutputStream : public roo_io::OutputStream {
 public:
  /// Creates a closed output stream.
  LinkOutputStream()
      : channel_(nullptr), my_stream_id_(0), status_(roo_io::kClosed) {}

  /// Creates an output stream bound to `my_stream_id` on `channel`.
  LinkOutputStream(Channel& channel, uint32_t my_stream_id)
      : channel_(&channel), my_stream_id_(my_stream_id), status_(roo_io::kOk) {}

  LinkOutputStream(const LinkOutputStream&) = delete;
  LinkOutputStream& operator=(const LinkOutputStream&) = delete;

  /// Moves stream ownership from `other`.
  LinkOutputStream(LinkOutputStream&& other);
  /// Moves stream ownership from `other`.
  LinkOutputStream& operator=(LinkOutputStream&& other);

  /// Writes up to `count` bytes, blocking if needed.
  size_t write(const roo::byte* buf, size_t count) override;

  /// Writes up to `count` bytes without blocking.
  size_t tryWrite(const roo::byte* buf, size_t count) override;

  /// Returns the number of bytes that can be written without blocking.
  size_t availableForWrite();

  /// Flushes pending output.
  void flush() override;

  /// Closes the stream.
  void close() override;

  /// Returns the current stream status.
  roo_io::Status status() const override { return status_; }

 private:
  Channel* channel_;
  uint32_t my_stream_id_;
  roo_io::Status status_;
};

}  // namespace roo_transport

#endif  // ROO_USE_THREADS