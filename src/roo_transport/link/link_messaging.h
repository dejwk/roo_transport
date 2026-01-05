#pragma once

#include <memory>

#include "roo_io/memory/store.h"
#include "roo_io/status.h"
#include "roo_logging.h"
#include "roo_threads.h"
#include "roo_threads/atomic.h"
#include "roo_threads/mutex.h"
#include "roo_transport/link/link_transport.h"
#include "roo_transport/messaging/messaging.h"

namespace roo_transport {

// Implementation of the Messaging interface over a LinkTransport.
class LinkMessaging : public Messaging {
 public:
  using Messaging::send;
  using Messaging::sendContinuation;

  LinkMessaging(roo_transport::LinkTransport& link_transport,
                size_t max_recv_packet_size,
                uint16_t recv_thread_stack_size = 4096,
                const char* recv_thread_name = "linkMsgRcv");

  void begin();

  void end();

  bool send(const roo::byte* header, size_t header_size,
            const roo::byte* payload, size_t payload_size,
            ConnectionId* connection_id) override;

  bool sendContinuation(ConnectionId connection_id, const roo::byte* header,
                        size_t header_size, const roo::byte* payload,
                        size_t payload_size) override;

 private:
  uint32_t connect();
  void receiveLoop();

  // Must hold mutex_.
  bool sendInternal(const roo::byte* header, size_t header_size,
                    const roo::byte* payload, size_t payload_size);

  roo_transport::LinkInputStream& in();

  roo_transport::LinkOutputStream& out();

  roo_transport::LinkTransport& transport_;
  roo_transport::Link link_;
  roo::atomic<bool> closed_ = false;

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