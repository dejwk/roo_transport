#pragma once

#include <memory>

#include "roo_io/core/output_stream.h"
#include "roo_transport/link/internal/thread_safe/channel.h"
#include "roo_transport/link/link.h"

namespace roo_transport {

class LinkTransport {
 public:
  LinkTransport(PacketSender& sender, PacketReceiver& receiver,
            unsigned int sendbuf_log2, unsigned int recvbuf_log2,
            std::string label = "");

  void begin() { channel_.begin(); }

  // Must be called when there is data to read.
  void readData();

  Link connect();
  Link connectAsync();

  uint32_t packets_sent() const { return channel_.packets_sent(); }
  uint32_t packets_delivered() const { return channel_.packets_delivered(); }
  uint32_t packets_received() const { return channel_.packets_received(); }

  // For single-threaded systems only.
  void loop();

 private:
  PacketSender& sender_;
  PacketReceiver& receiver_;

  Channel channel_;
};

}  // namespace roo_transport