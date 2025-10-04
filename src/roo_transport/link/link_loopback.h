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
      : pipe_client_to_server_(128),
        pipe_server_to_client_(128),
        server_input_(pipe_client_to_server_),
        server_output_(pipe_server_to_client_),
        client_input_(pipe_server_to_client_),
        client_output_(pipe_client_to_server_),
        server_packet_sender_(server_output_),
        server_packet_receiver_(server_input_),
        client_packet_sender_(client_output_),
        client_packet_receiver_(client_input_),
        server_(server_packet_sender_, kBufferSize4KB, kBufferSize4KB),
        client_(client_packet_sender_, kBufferSize4KB, kBufferSize4KB) {}

  // Returns the 'server' end of the loopback link.
  // Note: 'server' and 'client' are just conventional names here; they are in
  // fact fully symmetric.
  roo_transport::LinkTransport& server() { return server_; }

  // Returns the 'client' end of the loopback link.
  // Note: 'server' and 'client' are just conventional names here; they are in
  // fact fully symmetric.
  roo_transport::LinkTransport& client() { return client_; }

  bool serverReceive() {
    if (server_input_.status() != roo_io::kOk) return false;
    server_packet_receiver_.receive([this](const roo::byte* buf, size_t len) {
      server_.processIncomingPacket(buf, len);
    });
    return true;
  }

  bool clientReceive() {
    if (client_input_.status() != roo_io::kOk) return false;
    client_packet_receiver_.receive([this](const roo::byte* buf, size_t len) {
      client_.processIncomingPacket(buf, len);
    });
    return true;
  }

  // roo_io::Status in1_status() { return input_stream1_.status(); }
  // roo_io::Status in2_status() { return input_stream2_.status(); }

  // PacketReceiver& recv1() { return recv1_; }
  // PacketReceiver& recv2() { return recv2_; }

  void begin() {
    server_.begin();
    client_.begin();
  }

  void close() {
    server_output_.close();
    client_output_.close();
  }

 private:
  roo_io::RingPipe pipe_client_to_server_;
  roo_io::RingPipe pipe_server_to_client_;
  roo_io::RingPipeInputStream server_input_;
  roo_io::RingPipeOutputStream server_output_;
  roo_io::RingPipeInputStream client_input_;
  roo_io::RingPipeOutputStream client_output_;
  PacketSenderOverStream server_packet_sender_;
  PacketReceiverOverStream server_packet_receiver_;
  PacketSenderOverStream client_packet_sender_;
  PacketReceiverOverStream client_packet_receiver_;

  roo_transport::LinkTransport server_;
  roo_transport::LinkTransport client_;
};

}  // namespace roo_transport