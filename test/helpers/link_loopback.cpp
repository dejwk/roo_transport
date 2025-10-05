#include "link_loopback.h"

namespace roo_transport {

LinkLoopback::LinkLoopback() : LinkLoopback(128, 128) {}

LinkLoopback::LinkLoopback(size_t client_to_server_pipe_capacity,
                           size_t server_to_client_pipe_capacity)
    : pipe_client_to_server_(client_to_server_pipe_capacity),
      pipe_server_to_client_(server_to_client_pipe_capacity),
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

bool LinkLoopback::serverReceive() {
  if (server_input_.status() != roo_io::kOk) return false;
  server_packet_receiver_.receive([this](const roo::byte* buf, size_t len) {
    server_.processIncomingPacket(buf, len);
  });
  return true;
}

bool LinkLoopback::clientReceive() {
  if (client_input_.status() != roo_io::kOk) return false;
  client_packet_receiver_.receive([this](const roo::byte* buf, size_t len) {
    client_.processIncomingPacket(buf, len);
  });
  return true;
}

void LinkLoopback::begin() {
  server_.begin();
  client_.begin();
}

void LinkLoopback::close() {
  server_output_.close();
  client_output_.close();
}

}  // namespace roo_transport