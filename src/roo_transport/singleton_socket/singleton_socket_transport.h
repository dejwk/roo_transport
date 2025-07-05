#include <memory>

#include "roo_io/core/output_stream.h"
#include "roo_transport/singleton_socket/internal/thread_safe/channel.h"
#include "roo_transport/singleton_socket/singleton_socket.h"

namespace roo_io {

class SingletonSocketTransport {
 public:
  SingletonSocketTransport(roo_io::PacketSender& sender,
                           roo_io::PacketReceiver& receiver,
                           unsigned int sendbuf_log2, unsigned int recvbuf_log2,
                           std::string label);

  void begin() { channel_.begin(); }

  // Must be called when there is data to read.
  void readData();

  SingletonSocket* connect();
  SingletonSocket* connectAsync();

  uint32_t packets_sent() const { return channel_.packets_sent(); }
  uint32_t packets_delivered() const { return channel_.packets_delivered(); }
  uint32_t packets_received() const { return channel_.packets_received(); }

 private:
  roo_io::PacketSender& sender_;
  roo_io::PacketReceiver& receiver_;

  Channel channel_;
};

}  // namespace roo_io