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
                size_t max_recv_packet_size, Messaging::Receiver& receiver);

  ~LinkMessaging() override;

  void begin();
  void close();

  void send(const void* data, size_t size, SendMode send_mode) override;

 private:
  void connect();
  void receiveLoop();

  roo_transport::LinkInputStream& in();

  roo_transport::LinkOutputStream& out();

  roo_transport::LinkTransport& transport_;
  roo_transport::Link link_;
  uint32_t my_channel_id_;

  std::function<void(const void* data, size_t len)> recv_cb_;
  size_t max_recv_packet_size_;

  roo::thread reader_thread_;
  roo::condition_variable reconnected_;
  mutable roo::mutex mutex_;
  bool sender_disconnected_ = false;
  std::atomic<bool> closed_ = false;
};

}  // namespace roo_transport