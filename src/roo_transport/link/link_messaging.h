#pragma once

#include <memory>

#include "roo_io/memory/store.h"
#include "roo_io/status.h"
#include "roo_logging.h"
#include "roo_threads/mutex.h"
#include "roo_transport/link/link_transport.h"
#include "roo_transport/messaging/messaging.h"

namespace roo_transport {

// Implementation of the Messaging interface over a LinkTransport.
class LinkMessaging : public Messaging {
 public:
  LinkMessaging(roo_transport::LinkTransport& link_transport,
                size_t max_recv_packet_size,
                uint16_t recv_thread_stack_size = 4096,
                const char* recv_thread_name = "link_msg_recv");

  void begin() override;

  void end() override;

  ConnectionId send(ChannelId channel_id, const roo::byte* data,
                    size_t size) override;

  bool sendContinuation(ConnectionId connection_id, ChannelId channel_id,
                        const roo::byte* data, size_t size) override;

 private:
  uint32_t connect();
  void receiveLoop();

  // Must hold mutex_.
  void sendInternal(ChannelId channel_id, const roo::byte* data, size_t size);

  roo_transport::LinkInputStream& in();

  roo_transport::LinkOutputStream& out();

  roo_transport::LinkTransport& transport_;
  roo_transport::Link link_;
  std::atomic<bool> closed_ = false;

  std::function<void(ConnectionId connection_id, const roo::byte* data,
                     size_t len)>
      recv_cb_;
  size_t max_recv_packet_size_;
  uint16_t recv_thread_stack_size_;
  const char* recv_thread_name_;
  roo::thread reader_thread_;
  roo::condition_variable reconnected_;
  mutable roo::mutex mutex_;
};

}  // namespace roo_transport