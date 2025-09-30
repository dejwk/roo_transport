#pragma once

#include <memory>

#include "roo_io/core/output_stream.h"
#include "roo_transport/link/internal/thread_safe/channel.h"
#include "roo_transport/link/link.h"

namespace roo_transport {

class LinkTransport {
 public:
  LinkTransport(PacketSender& sender, unsigned int sendbuf_log2,
                unsigned int recvbuf_log2);

  // Starts the send thread.
  void begin() { channel_.begin(); }

  // Supply an incoming packet received from the underlying transport.
  void processIncomingPacket(const roo::byte* buf, size_t len);

  Link connect();
  Link connectAsync();

  uint32_t packets_sent() const { return channel_.packets_sent(); }
  uint32_t packets_delivered() const { return channel_.packets_delivered(); }
  uint32_t packets_received() const { return channel_.packets_received(); }

 private:
  PacketSender& sender_;
  Channel channel_;
};

}  // namespace roo_transport