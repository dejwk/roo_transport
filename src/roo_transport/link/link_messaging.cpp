#include "roo_transport/link/link_messaging.h"

namespace roo_transport {

LinkMessaging::LinkMessaging(roo_transport::LinkTransport& link_transport,
                             size_t max_recv_packet_size)
    : transport_(link_transport),
      link_(),
      max_recv_packet_size_(max_recv_packet_size) {}

void LinkMessaging::begin() {
  roo::thread::attributes attrs;
  attrs.set_name("messaging_receiver");
  attrs.set_stack_size(4096);
  reader_thread_ = roo::thread(attrs, [this]() { receiveLoop(); });
  LOG(INFO) << "Begin completed";
}

void LinkMessaging::send(const void* data, size_t size) {
  LOG(INFO) << "Sending...";
  roo::unique_lock<roo::mutex> guard(mutex_);
  while (true) {
    LinkStatus status = link_.status();
    if (status == LinkStatus::kConnected || status == LinkStatus::kConnecting) {
      break;
    }
    reconnected_.wait(guard);
  }
  LOG(INFO) << "Got output stream";
  roo_io::OutputStream& out = this->out();
  // if (socket_.isConnecting()) {
  //   LOG(INFO) << "Still connecting; await";
  //   socket_.awaitConnected();
  //   LOG(INFO) << "Connected";
  // }
  // if (out.status() != roo_io::kOk) {
  //   LOG(INFO) << out.status();
  //   connect();
  // }
  roo::byte header[4];
  roo_io::StoreBeU32(size, header);
  out.writeFully(header, 4);
  out.writeFully((const roo::byte*)data, size);
  out.flush();
}

void LinkMessaging::registerRecvCallback(
    std::function<void(const void* data, size_t len)> cb) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  recv_cb_ = cb;
}

void LinkMessaging::connect() {
  LOG(INFO) << "Connecting...";
  roo::lock_guard<roo::mutex> guard(mutex_);
  //   while (true) {
  //     LinkStatus status = link_.status();
  //     if (status == LinkStatus::kConnected || status ==
  //     LinkStatus::kConnecting) {
  //       // Already connected or in the process of connecting.
  //       break;
  //     }
  link_ = transport_.connectAsync();
  reconnected_.notify_all();
  LOG(INFO) << "Notifying...";
  // link_.in().onReceive([this]() { received(); });
  //   }
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
  while (true) {
    connect();
    roo_io::InputStream& in = this->in();
    while (true) {
      roo::byte header[4];
      size_t count = in.readFully(header, 4);
      if (count < 4) {
        LOG(ERROR) << "Error: " << in.status();
        break;
      }
      uint32_t incoming_size = roo_io::LoadBeU32(header);
      if (incoming_size > max_recv_packet_size_) {
        LOG(ERROR) << "Error: incoming size " << incoming_size
                   << " exceeds max " << max_recv_packet_size_;
        break;
      }
      size_t read = in.readFully(incoming_payload.get(), incoming_size);
      if (read < incoming_size) {
        LOG(ERROR) << "Error: " << in.status();
        break;
      }
      std::function<void(const void* data, size_t len)> recv_cb;
      {
        roo::lock_guard<roo::mutex> guard(mutex_);
        recv_cb = recv_cb_;
      }
      if (recv_cb != nullptr) recv_cb(incoming_payload.get(), incoming_size);
    }
  }
}

// void LinkMessaging::received() {
//   SocketInputStream& in = this->in();
//   while (true) {
//     if (incoming_size_ < 0) {
//       // Header not yet fully read.
//       pos_ += in.tryRead(incoming_header_ + pos_, 4 - pos_);
//       if (pos_ < 4) {
//         // Still incomplete.
//         if (in.status() != roo_io::kOk) {
//           pos_ = 0;
//           LOG(ERROR) << "Error: " << in.status();
//           connect();
//         }
//         return;
//       }
//       incoming_size_ = roo_io::LoadBeU32(incoming_header_);
//       CHECK_LE(incoming_size_, max_recv_packet_size_);
//       pos_ = 0;
//     }
//     while (pos_ < incoming_size_) {
//       size_t read =
//           in.tryRead(incoming_payload_.get() + pos_, incoming_size_ - pos_);
//       if (read == 0) {
//         if (in.status() != roo_io::kOk) {
//           pos_ = 0;
//           incoming_size_ = -1;
//           LOG(ERROR) << "Error: " << in.status();
//           connect();
//         }
//         return;
//       }
//       pos_ += read;
//     }
//     recv_cb_(incoming_payload_.get(), incoming_size_);
//     // Reset state for reading the next packet.
//     pos_ = 0;
//     incoming_size_ = -1;
//   }
// }

}  // namespace roo_transport