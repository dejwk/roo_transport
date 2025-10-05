#pragma once

#include "noisy_output_stream.h"
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
  LinkLoopback();

  LinkLoopback(size_t client_to_server_pipe_capacity,
               size_t server_to_client_pipe_capacity);

  ~LinkLoopback();

  // Returns the 'server' end of the loopback link.
  // Note: 'server' and 'client' are just conventional names here; they are in
  // fact fully symmetric.
  roo_transport::LinkTransport& server() { return server_; }

  // Returns the 'client' end of the loopback link.
  // Note: 'server' and 'client' are just conventional names here; they are in
  // fact fully symmetric.
  roo_transport::LinkTransport& client() { return client_; }

  void setServerOutputErrorRate(int error_rate) {
    noisy_server_output_.setErrorRate(error_rate);
  }

  void setClientOutputErrorRate(int error_rate) {
    noisy_client_output_.setErrorRate(error_rate);
  }

  bool serverReceive();

  bool clientReceive();

  void begin();

  void close();

 private:
  roo_io::RingPipe pipe_client_to_server_;
  roo_io::RingPipe pipe_server_to_client_;
  roo_io::RingPipeInputStream server_input_;
  roo_io::RingPipeOutputStream server_output_;
  NoisyOutputStream noisy_server_output_;
  roo_io::RingPipeInputStream client_input_;
  roo_io::RingPipeOutputStream client_output_;
  NoisyOutputStream noisy_client_output_;
  PacketSenderOverStream server_packet_sender_;
  PacketReceiverOverStream server_packet_receiver_;
  PacketSenderOverStream client_packet_sender_;
  PacketReceiverOverStream client_packet_receiver_;

  roo_transport::LinkTransport server_;
  roo_transport::LinkTransport client_;

  roo::thread server_receiving_thread_;
  roo::thread client_receiving_thread_;
};

}  // namespace roo_transport