#pragma once

#include "roo_io/ringpipe/ringpipe.h"
#include "roo_io/ringpipe/ringpipe_input_stream.h"
#include "roo_io/ringpipe/ringpipe_output_stream.h"
#include "roo_transport.h"
#include "roo_transport/link/link_transport.h"
#include "roo_transport/packets/over_stream/packet_receiver_over_stream.h"
#include "roo_transport/packets/over_stream/packet_sender_over_stream.h"
namespace roo_transport {

class LinkLoopback {
 public:
  LinkLoopback()
      : pipe1_(128),
        pipe2_(128),
        input_stream1_(pipe1_),
        output_stream1_(pipe2_),
        input_stream2_(pipe2_),
        output_stream2_(pipe1_),
        send1_(output_stream1_),
        recv1_(input_stream1_),
        send2_(output_stream2_),
        recv2_(input_stream2_),
        t1_(send1_, 4, 4),
        t2_(send2_, 4, 4) {}

  roo_transport::LinkTransport& t1() { return t1_; }
  roo_transport::LinkTransport& t2() { return t2_; }

  bool receive1() {
    if (input_stream1_.status() != roo_io::kOk) return false;
    recv1_.receive([this](const roo::byte* buf, size_t len) {
      t1_.processIncomingPacket(buf, len);
    });
    return true;
  }

  bool receive2() {
    if (input_stream2_.status() != roo_io::kOk) return false;
    recv2_.receive([this](const roo::byte* buf, size_t len) {
      t2_.processIncomingPacket(buf, len);
    });
    return true;
  }

  // roo_io::Status in1_status() { return input_stream1_.status(); }
  // roo_io::Status in2_status() { return input_stream2_.status(); }

  // PacketReceiver& recv1() { return recv1_; }
  // PacketReceiver& recv2() { return recv2_; }

  void begin() {
    t1_.begin();
    t2_.begin();
  }

  void close() {
    output_stream1_.close();
    output_stream2_.close();
  }

 private:
  roo_io::RingPipe pipe1_;
  roo_io::RingPipe pipe2_;
  roo_io::RingPipeInputStream input_stream1_;
  roo_io::RingPipeOutputStream output_stream1_;
  roo_io::RingPipeInputStream input_stream2_;
  roo_io::RingPipeOutputStream output_stream2_;
  PacketSenderOverStream send1_;
  PacketReceiverOverStream recv1_;
  PacketSenderOverStream send2_;
  PacketReceiverOverStream recv2_;

  roo_transport::LinkTransport t1_;
  roo_transport::LinkTransport t2_;
};

}  // namespace roo_transport