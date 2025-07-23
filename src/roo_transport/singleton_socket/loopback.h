#pragma once

#include "roo_transport.h"
#include "roo_transport/packets/loopback/loopback_packet_receiver.h"
#include "roo_transport/packets/loopback/loopback_packet_sender.h"
#include "roo_transport/singleton_socket/singleton_socket_transport.h"

namespace roo_transport {

class SingletonSocketLoopback {
 public:
  SingletonSocketLoopback()
      : recv1_(),
        recv2_(),
        send1_(recv2_),
        send2_(recv1_),
        t1_(send1_, recv1_, 4, 4),
        t2_(send2_, recv2_, 4, 4) {}

  roo_transport::SingletonSocketTransport& t1() { return t1_; }
  roo_transport::SingletonSocketTransport& t2() { return t2_; }

 private:
  roo_transport::LoopbackPacketReceiver recv1_;
  roo_transport::LoopbackPacketReceiver recv2_;
  roo_transport::LoopbackPacketSender send1_;
  roo_transport::LoopbackPacketSender send2_;
  roo_transport::SingletonSocketTransport t1_;
  roo_transport::SingletonSocketTransport t2_;
};

}  // namespace roo_transport