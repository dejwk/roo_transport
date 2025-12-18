#include "roo_transport/link/link_messaging.h"

namespace roo_transport {

LinkMessaging::LinkMessaging(roo_transport::LinkTransport& link_transport,
                             size_t max_recv_packet_size,
                             uint16_t recv_thread_stack_size,
                             const char* recv_thread_name)
    : transport_(link_transport),
      link_(),
      closed_(false),
      max_recv_packet_size_(max_recv_packet_size),
      recv_thread_stack_size_(recv_thread_stack_size),
      recv_thread_name_(recv_thread_name) {}

void LinkMessaging::begin() {
  roo::thread::attributes attrs;
  attrs.set_name(recv_thread_name_);
  attrs.set_stack_size(recv_thread_stack_size_);
  reader_thread_ = roo::thread(attrs, [this]() { receiveLoop(); });
}

void LinkMessaging::end() {
  if (closed_) return;
  closed_ = true;
  {
    roo::lock_guard<roo::mutex> guard(mutex_);
    link_.disconnect();
    reconnected_.notify_all();
  }
  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }
}

bool LinkMessaging::send(const roo::byte* header, size_t header_size,
                         const roo::byte* payload, size_t payload_size,
                         Messaging::ConnectionId* connection_id) {
  roo::unique_lock<roo::mutex> guard(mutex_);
  while (true) {
    LinkStatus status = link_.status();
    if (status == LinkStatus::kConnected || status == LinkStatus::kConnecting) {
      if (connection_id != nullptr) {
        *connection_id = (Messaging::ConnectionId)link_.streamId();
      }
      break;
    }
    reconnected_.wait(guard);
  }
  return sendInternal(header, header_size, payload, payload_size);
}

bool LinkMessaging::sendContinuation(ConnectionId connection_id,
                                     const roo::byte* header,
                                     size_t header_size,
                                     const roo::byte* payload,
                                     size_t payload_size) {
  roo::unique_lock<roo::mutex> guard(mutex_);
  if ((ConnectionId)link_.streamId() != connection_id) {
    // Connection ID does not match the current link stream ID; the connection
    // must have been reset.
    return false;
  }
  return sendInternal(header, header_size, payload, payload_size);
}

bool LinkMessaging::sendInternal(const roo::byte* header, size_t header_size,
                                 const roo::byte* payload,
                                 size_t payload_size) {
  roo_io::OutputStream& out = link_.out();
  roo::byte serialized_size[4];
  roo_io::StoreBeU32(header_size + payload_size, serialized_size);
  out.writeFully(serialized_size, 4);
  out.writeFully(header, header_size);
  out.writeFully(payload, payload_size);
  out.flush();
  return out.isOpen();
}

uint32_t LinkMessaging::connect() {
  roo::lock_guard<roo::mutex> guard(mutex_);
  link_ = transport_.connectAsync();
  reconnected_.notify_all();
  return link_.streamId();
}

roo_transport::LinkInputStream& LinkMessaging::in() {
  roo::lock_guard<roo::mutex> guard(mutex_);
  return link_.in();
}

roo_transport::LinkOutputStream& LinkMessaging::out() {
  roo::lock_guard<roo::mutex> guard(mutex_);
  return link_.out();
}

void LinkMessaging::receiveLoop() {
  std::unique_ptr<roo::byte[]> incoming_payload(
      new roo::byte[max_recv_packet_size_]);
  while (!closed_) {
    ConnectionId connection_id = (ConnectionId)connect();
    roo_io::InputStream& in = this->in();
    while (true) {
      roo::byte serialized_size[4];
      size_t count = in.readFully(serialized_size, 4);
      if (count < 4) {
        LOG(ERROR) << "Error: " << in.status();
        reset(connection_id);
        break;
      }
      uint32_t incoming_size = roo_io::LoadBeU32(serialized_size);
      if (incoming_size > max_recv_packet_size_) {
        LOG(ERROR) << "Error: incoming size " << incoming_size
                   << " exceeds max " << max_recv_packet_size_;
        reset(connection_id);
        break;
      }
      size_t read = in.readFully(incoming_payload.get(), incoming_size);
      if (read < incoming_size) {
        LOG(ERROR) << "Error: " << in.status();
        reset(connection_id);
        break;
      }
      received(connection_id, incoming_payload.get(), incoming_size);
    }
  }
}

}  // namespace roo_transport