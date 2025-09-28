#pragma once

#include <memory>

#include "roo_io/memory/store.h"
#include "roo_io/status.h"
#include "roo_logging.h"
#include "roo_transport/link/link_transport.h"
#include "roo_transport/messaging/messaging.h"
#include "roo_transport/socket_input_stream.h"
#include "roo_transport/socket_output_stream.h"

namespace roo_transport {

class LinkMessaging : public Messaging {
 public:
  LinkMessaging(roo_transport::LinkTransport& link_transport,
                size_t max_recv_packet_size)
      : transport_(link_transport),
        link_(),
        max_recv_packet_size_(max_recv_packet_size),
        incoming_size_(-1),
        pos_(0),
        incoming_payload_(new roo::byte[max_recv_packet_size]) {}

  void begin() { connect(); }

  void send(const void* data, size_t size) override {
    // if (socket_.isConnecting()) {
    //   LOG(INFO) << "Still connecting; await";
    //   socket_.awaitConnected();
    //   LOG(INFO) << "Connected";
    // }
    if (out().status() != roo_io::kOk) {
      LOG(INFO) << out().status();
      connect();
    }
    roo::byte header[4];
    roo_io::StoreBeU32(size, header);
    out().writeFully(header, 4);
    out().writeFully((const roo::byte*)data, size);
    out().flush();
  }

  void registerRecvCallback(
      std::function<void(const void* data, size_t len)> cb) override {
    recv_cb_ = cb;
  }

 private:
  void connect() {
    link_ = transport_.connectAsync();
    in().onReceive([this]() { received(); });
  }

  roo_transport::SocketInputStream& in() { return link_.in(); }

  roo_transport::SocketOutputStream& out() { return link_.out(); }

  void received() {
    while (true) {
      if (incoming_size_ < 0) {
        // Header not yet fully read.
        pos_ += in().tryRead(incoming_header_ + pos_, 4 - pos_);
        if (pos_ < 4) {
          // Still incomplete.
          if (in().status() != roo_io::kOk) {
            pos_ = 0;
            LOG(ERROR) << "Error: " << in().status();
            connect();
          }
          return;
        }
        incoming_size_ = roo_io::LoadBeU32(incoming_header_);
        CHECK_LE(incoming_size_, max_recv_packet_size_);
        pos_ = 0;
      }
      while (pos_ < incoming_size_) {
        size_t read =
            in().tryRead(incoming_payload_.get() + pos_, incoming_size_ - pos_);
        if (read == 0) {
          if (in().status() != roo_io::kOk) {
            pos_ = 0;
            incoming_size_ = -1;
            LOG(ERROR) << "Error: " << in().status();
            connect();
          }
          return;
        }
        pos_ += read;
      }
      recv_cb_(incoming_payload_.get(), incoming_size_);
      // Reset state for reading the next packet.
      pos_ = 0;
      incoming_size_ = -1;
    }
  }

  roo_transport::LinkTransport& transport_;
  roo_transport::Link link_;
  uint32_t my_channel_id_;

  std::function<void(const void* data, size_t len)> recv_cb_;
  size_t max_recv_packet_size_;
  roo::byte incoming_header_[4];
  int incoming_size_;
  int pos_;
  std::unique_ptr<roo::byte[]> incoming_payload_;
};

}  // namespace roo_transport