#pragma once

#include "roo_io.h"
#include "roo_io/core/output_stream.h"
#include "roo_transport.h"
#include "roo_transport/link/internal/thread_safe/channel.h"
#include "roo_transport/link/internal/thread_safe/channel_input.h"
#include "roo_transport/link/internal/thread_safe/channel_output.h"

namespace roo_transport {

class Link {
 public:
  Link();

  // Status status() const;

  // Obtains the input stream that can be used to read from the link.
  SocketInputStream& in() { return in_; }

  // Obtains the output stream that can be used to write to the link.
  SocketOutputStream& out() { return out_; }

  // Returns whether the link is currently in process of establishing a
  // connection with the peer.
  bool isConnecting();

  void awaitConnected();
  bool awaitConnected(roo_time::Interval timeout);

 private:
  friend class SerialLink;
  friend class LinkTransport;

  Link(Channel& channel, uint32_t my_stream_id);

  Channel* channel_;
  uint32_t my_stream_id_;
  ChannelInput in_;
  ChannelOutput out_;
};

}  // namespace roo_transport