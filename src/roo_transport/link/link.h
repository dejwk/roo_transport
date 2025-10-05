#pragma once

#include "roo_io.h"
#include "roo_io/core/output_stream.h"
#include "roo_transport.h"
#include "roo_transport/link/internal/thread_safe/channel.h"
#include "roo_transport/link/link_input_stream.h"
#include "roo_transport/link/link_output_stream.h"
#include "roo_transport/link/link_status.h"

namespace roo_transport {

// Represents a reliable bidirectional peer-to-peer link over a packet-based
// transport.
class Link {
 public:
  // Creates a dummy link in state kIdle.
  // Use LinkTransport::connect() to create a proper connected link.
  Link();

  // Obtains the input stream that can be used to read from the link.
  SocketInputStream& in() { return in_; }

  // Obtains the output stream that can be used to write to the link.
  SocketOutputStream& out() { return out_; }

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

  // Disconnects the link, immediately bringing it to the idle state. If the
  // link is already idle, does nothing.
  void disconnect();

 private:
  friend class SerialLink;
  friend class LinkTransport;

  Link(Channel& channel, uint32_t my_stream_id);

  Channel* channel_;
  uint32_t my_stream_id_;
  LinkInputStream in_;
  LinkOutputStream out_;
};

}  // namespace roo_transport