#pragma once

#include "roo_io.h"
#include "roo_io/core/output_stream.h"
#include "roo_transport.h"
#include "roo_transport/link/internal/thread_safe/channel.h"
#include "roo_transport/link/link_input_stream.h"
#include "roo_transport/link/link_output_stream.h"
#include "roo_transport/link/link_status.h"

namespace roo_transport {

/// Reliable bidirectional peer-to-peer link over a packet-based transport.
class Link {
 public:
  /// Creates a dummy detached link in state `kIdle`.
  ///
  /// Use `LinkTransport::connect()` or `connectAsync()` to obtain a connected
  /// link backed by a live channel.
  Link();

  Link(const Link&) = delete;
  Link& operator=(const Link&) = delete;

  /// Moves link ownership from `other`.
  Link(Link&& other);

  /// Moves link ownership from `other`.
  Link& operator=(Link&& other);

  /// Returns the input stream for reading from the link.
  LinkInputStream& in() { return in_; }

  /// Returns the output stream for writing to the link.
  LinkOutputStream& out() { return out_; }

  /// Returns the current link status.
  ///
  /// A detached link reports `kIdle`.
  LinkStatus status() const;

  /// Waits until a connecting link becomes connected or broken.
  ///
  /// If the link is idle, already connected, or already broken, this returns
  /// immediately.
  void awaitConnected();

  /// Waits for the link state to change or for `timeout` to elapse.
  ///
  /// If the link is idle, connected, or broken, returns `true` immediately.
  /// While the link is in `kConnecting`, blocks until it becomes connected or
  /// broken, or until the timeout expires.
  bool awaitConnected(roo_time::Duration timeout);

  /// Disconnects the link and returns it to the idle state.
  ///
  /// If the link is already idle, this does nothing.
  void disconnect();

  /// Returns the local stream id assigned to this link.
  ///
  /// Links created by `LinkTransport::connect()` and `connectAsync()` receive
  /// unique stream ids.
  uint32_t streamId() const { return my_stream_id_; }

 private:
  friend class LinkStream;
  friend class LinkTransport;

  Link(Channel& channel, uint32_t my_stream_id);

  Channel* channel_;
  uint32_t my_stream_id_;
  LinkInputStream in_;
  LinkOutputStream out_;
};

}  // namespace roo_transport