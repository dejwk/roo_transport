#pragma once

#include <memory>

#include "roo_io/core/output_stream.h"
#include "roo_transport/link/internal/thread_safe/channel.h"
#include "roo_transport/link/link.h"
#include "roo_transport/link/link_buffer_size.h"

namespace roo_transport {

class LinkTransport {
 public:
  class StatsMonitor;

  LinkTransport(PacketSender& sender, LinkBufferSize sendbuf = kBufferSize4KB,
                LinkBufferSize recvbuf = kBufferSize4KB);

  LinkTransport(PacketSender& sender, roo::string_view name,
                LinkBufferSize sendbuf = kBufferSize4KB,
                LinkBufferSize recvbuf = kBufferSize4KB);

  // Starts the send thread.
  void begin() { channel_.begin(); }

  void end() { channel_.end(); }

  // Supply an incoming packet received from the underlying transport.
  void processIncomingPacket(const roo::byte* buf, size_t len);

  // Establishes a new connection and returns the Link object representing it.
  // The optional function parameter will be called when the link gets
  // disconnected.
  Link connect(std::function<void()> disconnect_fn = nullptr);

  // Establishes a new connection asynchronously and returns the Link object
  // representing it. Until the connection is established, the link will be in
  // the "connecting" state.
  // The optional function parameter will be called when the link gets
  // disconnected.
  Link connectAsync(std::function<void()> disconnect_fn = nullptr);

  // Establishes a new connection and returns the Link object representing it.
  // If the peer attempts reconnection (e.g. after a reset), the program
  // will terminate (usually to reconnect after reboot).
  Link connectOrDie() {
    return connect(
        []() { LOG(FATAL) << "LinkTransport: peer reset; rebooting"; });
  }

 private:
  friend class StatsMonitor;

  PacketSender& sender_;
  Channel channel_;
};

class LinkTransport::StatsMonitor {
 public:
  StatsMonitor(LinkTransport& transport) : channel_(transport.channel_) {}

  // Returns the count of packets sent over the link transport since start
  // (including retransmissions). The counter does not reset on new connections.
  uint32_t packets_sent() const { return channel_.packets_sent(); }

  // Returns the count of packets confirmed as delivered by the peer since
  // start. The counter does not reset on new connections.
  uint32_t packets_delivered() const { return channel_.packets_delivered(); }

  // Returns the count of packets received from the peer since start. The
  // counter does not reset on new connections.
  uint32_t packets_received() const { return channel_.packets_received(); }

 private:
  Channel& channel_;
};

}  // namespace roo_transport