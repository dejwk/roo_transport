#pragma once

#if (defined ARDUINO)

#include "Arduino.h"
#include "roo_transport/link/link.h"

namespace roo_transport {

// Link wrapped in the arduino Stream interface. Represents a reliable
// bidirectional stream, e.g. over Serial.
class LinkStream : public Stream {
 public:
  // Creates a dummy stream link in state kIdle.
  // Use SerialLinkTransport::connect() to create a proper connected link.
  LinkStream() = default;

  // Wrap a link into StreamLink.
  LinkStream(Link link);

  int available() override;
  int read() override;
  int peek() override;

  size_t readBytes(char* buffer, size_t length) override;
  size_t readBytes(uint8_t* buffer, size_t length) override;

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  int availableForWrite() override;
  void flush() override;

  // Obtains the input stream that can be used to read from the reliable
  // stream.
  LinkInputStream& in() { return link_.in(); }

  // Obtains the output stream that can be used to write to the reliable
  // stream.
  LinkOutputStream& out() { return link_.out(); }

  // Returns the current status of the link.
  LinkStatus status() const;

  // If the link is in state kConnecting, blocks until it becomes either
  // kConnected or kBroken. Otherwise, returns immediately.
  void awaitConnected();

  // If the link is kIdle, kConnected, or kBroken, does nothing and returns
  // true immediately. Otherwise (when the link is in state kConnecting), blocks
  // until it becomes either kConnected or kBroken, or until the specified
  // timeout elapses, and returns true if the link has changed state, and false
  // if the timeout has elapsed.
  bool awaitConnected(roo_time::Duration timeout);

  // Retrieves the underlying link.
  Link& link() { return link_; }

  // Retrieves the underlying link.
  const Link& link() const { return link_; }

 private:
  friend class StreamLinkTransport;

  LinkStream(Channel& channel, uint32_t my_stream_id);

  size_t timedRead(roo::byte* buf, size_t count, roo_time::Duration timeout);

  Link link_;
};

}  // namespace roo_transport

#endif  // defined(ARDUINO)