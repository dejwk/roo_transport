#pragma once

#include <memory>

#include "roo_io/core/output_stream.h"
#include "roo_transport/link/internal/thread_safe/channel.h"
#include "roo_transport/link/link.h"
#include "roo_transport/link/link_buffer_size.h"

namespace roo_transport {

/// Reliable link transport built on top of a packet sender.
class LinkTransport {
 public:
  class StatsMonitor;

  /// Creates a transport with unnamed link logging.
  ///
  /// `sendbuf` and `recvbuf` configure the reliable stream windows used by the
  /// underlying channel.
  LinkTransport(PacketSender& sender, LinkBufferSize sendbuf = kBufferSize4KB,
                LinkBufferSize recvbuf = kBufferSize4KB);

  /// Creates a transport with the supplied diagnostic `name`.
  ///
  /// `name` is used only for diagnostics and logging.
  LinkTransport(PacketSender& sender, roo::string_view name,
                LinkBufferSize sendbuf = kBufferSize4KB,
                LinkBufferSize recvbuf = kBufferSize4KB);

  /// Starts the transport send thread.
  void begin() { channel_.begin(); }

  /// Stops the transport send thread.
  void end() { channel_.end(); }

  /// Passes one received packet into the transport.
  ///
  /// Call this for every packet that arrives from the underlying medium so the
  /// channel can process handshakes, ACKs, flow-control updates, and payloads.
  void processIncomingPacket(const roo::byte* buf, size_t len);

  /// Establishes a new connection and returns the resulting link.
  ///
  /// Blocks until the handshake finishes. If supplied, `disconnect_fn` is
  /// called when that link is later disconnected.
  Link connect(std::function<void()> disconnect_fn = nullptr);

  /// Establishes a new connection without waiting for completion.
  ///
  /// The returned link initially remains in `kConnecting` until the handshake
  /// completes. If supplied, `disconnect_fn` is called when that link is later
  /// disconnected.
  Link connectAsync(std::function<void()> disconnect_fn = nullptr);

  /// Establishes a new connection and terminates if the peer later resets.
  Link connectOrDie() {
    return connect(
        []() { LOG(FATAL) << "LinkTransport: peer reset; rebooting"; });
  }

 private:
  friend class StatsMonitor;

  PacketSender& sender_;
  Channel channel_;
};

/// Snapshot access to packet-level transport counters.
class LinkTransport::StatsMonitor {
 public:
  /// Creates a stats view for `transport`.
  StatsMonitor(LinkTransport& transport) : channel_(transport.channel_) {}

  /// Returns sent packet count, including retransmissions.
  ///
  /// The counter is cumulative and does not reset on new connections.
  uint32_t packets_sent() const { return channel_.packets_sent(); }

  /// Returns packets confirmed as delivered by the peer.
  ///
  /// The counter is cumulative and does not reset on new connections.
  uint32_t packets_delivered() const { return channel_.packets_delivered(); }

  /// Returns packets received from the peer.
  ///
  /// The counter is cumulative and does not reset on new connections.
  uint32_t packets_received() const { return channel_.packets_received(); }

 private:
  Channel& channel_;
};

}  // namespace roo_transport