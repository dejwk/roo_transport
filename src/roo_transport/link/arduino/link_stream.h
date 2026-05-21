#pragma once

#if (defined ARDUINO)

#include "Arduino.h"
#include "roo_transport/link/link.h"

namespace roo_transport {

/// Arduino `Stream` wrapper over a reliable bidirectional `Link`.
///
/// Represents a reliable bidirectional stream, for example over Serial.
class LinkStream : public Stream {
 public:
  /// Creates a detached stream in the idle state.
  ///
  /// Use `LinkStreamTransport::connect()` to obtain a connected stream.
  LinkStream() = default;

  /// Wraps an existing `Link` in the Arduino `Stream` API.
  LinkStream(Link link);

  /// Returns bytes available for immediate reading.
  int available() override;
  /// Reads one byte from the stream.
  int read() override;
  /// Peeks at the next byte without consuming it.
  int peek() override;

#ifdef ESP_PLATFORM
  /// Reads up to `length` bytes into `buffer`.
  size_t readBytes(char* buffer, size_t length) override;
  /// Reads up to `length` bytes into `buffer`.
  size_t readBytes(uint8_t* buffer, size_t length) override;
#else
  /// Reads up to `length` bytes into `buffer`.
  size_t readBytes(char* buffer, size_t length);
  /// Reads up to `length` bytes into `buffer`.
  size_t readBytes(uint8_t* buffer, size_t length);
#endif

  /// Writes one byte to the stream.
  size_t write(uint8_t) override;
  /// Writes `size` bytes to the stream.
  size_t write(const uint8_t* buffer, size_t size) override;
  /// Returns bytes that can be written without blocking.
  int availableForWrite() override;
  /// Flushes pending output.
  void flush() override;

  /// Returns the input side of the underlying link.
  LinkInputStream& in() { return link_.in(); }

  /// Returns the output side of the underlying link.
  LinkOutputStream& out() { return link_.out(); }

  /// Returns the current link status.
  LinkStatus status() const;

  /// Waits until a connecting link becomes connected or broken.
  ///
  /// If the stream is idle, already connected, or already broken, this returns
  /// immediately.
  void awaitConnected();

  /// Waits for the link state to change or for `timeout` to elapse.
  ///
  /// If the stream is idle, connected, or broken, returns `true`
  /// immediately. While the stream is in `kConnecting`, blocks until it
  /// becomes connected or broken, or until the timeout expires.
  bool awaitConnected(roo_time::Duration timeout);

  /// Returns the wrapped link.
  Link& link() { return link_; }

  /// Returns the wrapped link.
  const Link& link() const { return link_; }

 protected:
  void set(Link&& link) { link_ = std::move(link); }

 private:
  friend class StreamLinkTransport;

  LinkStream(Channel& channel, uint32_t my_stream_id);

  size_t timedRead(roo::byte* buf, size_t count, roo_time::Duration timeout);

  Link link_;
};

}  // namespace roo_transport

#endif  // defined(ARDUINO)