#pragma once

#include <memory>

#include "roo_io/core/output_stream.h"
#include "roo_transport/link/internal/thread_safe/channel.h"
#include "roo_transport/link/link.h"
#include "roo_transport/link/link_buffer_size.h"

namespace roo_transport {

class LinkTransport {
 public:
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

  Link connect(std::function<void()> disconnect_fn = nullptr);
  Link connectAsync(std::function<void()> disconnect_fn = nullptr);

  uint32_t packets_sent() const { return channel_.packets_sent(); }
  uint32_t packets_delivered() const { return channel_.packets_delivered(); }
  uint32_t packets_received() const { return channel_.packets_received(); }

 private:
  PacketSender& sender_;
  Channel channel_;
};

}  // namespace roo_transport