#include "roo_transport/link/link_messaging.h"

namespace roo_transport {

LinkMessaging::LinkMessaging(roo_transport::LinkTransport& link_transport,
                             size_t max_recv_packet_size)
    : transport_(link_transport),
      link_(),
      max_recv_packet_size_(max_recv_packet_size),
      sender_disconnected_(true),
      closed_(false) {}

void LinkMessaging::begin() {
  roo::thread::attributes attrs;
  attrs.set_name("link_msg_recv");
  attrs.set_stack_size(4096);
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

void LinkMessaging::send(ChannelId channel_id, const roo::byte* data,
                         size_t size) {
  sendInternal(channel_id, data, size, false);
}

void LinkMessaging::sendContinuation(ChannelId channel_id,
                                     const roo::byte* data, size_t size) {
  sendInternal(channel_id, data, size, true);
}

void LinkMessaging::sendInternal(ChannelId channel_id, const roo::byte* data,
                                 size_t size, bool continuation) {
  // Receiver may reconnect and thus reset the link object at any time, so we
  // need to hold the lock while sending to not let that happen until we're done
  // sending.
  roo::unique_lock<roo::mutex> guard(mutex_);
  while (sender_disconnected_) {
    if (continuation) {
      return;
    }
    LinkStatus status = link_.status();
    if (status == LinkStatus::kConnected || status == LinkStatus::kConnecting) {
      sender_disconnected_ = false;
      break;
    }
    reconnected_.wait(guard);
  }
  roo_io::OutputStream& out = link_.out();
  roo::byte header[4];
  roo_io::StoreBeU32(size, header);
  out.writeFully(header, 4);
  out.write((const roo::byte*)&channel_id, 1);
  out.writeFully((const roo::byte*)data, size);
  out.flush();
}

void LinkMessaging::connect() {
  roo::lock_guard<roo::mutex> guard(mutex_);
  sender_disconnected_ = true;
  link_ = transport_.connectAsync();
  reconnected_.notify_all();
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
    connect();
    roo_io::InputStream& in = this->in();
    while (true) {
      roo::byte header[4];
      size_t count = in.readFully(header, 4);
      if (count < 4) {
        LOG(ERROR) << "Error: " << in.status();
        reset();
        break;
      }
      uint32_t incoming_size = roo_io::LoadBeU32(header);
      if (incoming_size > max_recv_packet_size_) {
        LOG(ERROR) << "Error: incoming size " << incoming_size
                   << " exceeds max " << max_recv_packet_size_;
        reset();
        break;
      }
      size_t read = in.readFully(incoming_payload.get(), incoming_size + 1);
      if (read < incoming_size + 1) {
        LOG(ERROR) << "Error: " << in.status();
        reset();
        break;
      }
      ChannelId channel_id = (ChannelId)incoming_payload[0];
      received(channel_id, &incoming_payload[1], incoming_size);
    }
  }
}

}  // namespace roo_transport