#include <memory>

#include "roo_io/core/output_stream.h"
#include "roo_transport/bidi_streaming/internal/thread_safe/channel.h"
#include "roo_transport/bidi_streaming/internal/thread_safe/channel_input.h"
#include "roo_transport/bidi_streaming/internal/thread_safe/channel_output.h"

namespace roo_io {

class SingletonSocket {
 public:
  SingletonSocket(Channel& channel, uint32_t my_stream_id);

  // Status status() const;

  // Obtains the input stream that can be used to read from the socket.
  roo_io::InputStream& in() { return in_; }

  // Obtains the output stream that can be used to write to the socket.
  roo_io::OutputStream& out() { return out_; }

  bool isConnecting();

  void awaitConnected();
  bool awaitConnected(roo_time::Interval timeout);

 private:
  Channel& channel_;
  uint32_t my_stream_id_;
  ChannelInput in_;
  ChannelOutput out_;
};

}  // namespace roo_io