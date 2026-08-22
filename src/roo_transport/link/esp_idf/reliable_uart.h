#pragma once

#if defined(ESP_PLATFORM) && !defined(ARDUINO)

#include <functional>
#include <string>

#include "hal/uart_types.h"
#include "roo_io/uart/esp32/uart_input_stream.h"
#include "roo_io/uart/esp32/uart_output_stream.h"
#include "roo_threads.h"
#include "roo_threads/atomic.h"
#include "roo_transport/link/link.h"
#include "roo_transport/link/link_transport.h"
#include "roo_transport/packets/over_stream/packet_receiver_over_stream.h"
#include "roo_transport/packets/over_stream/packet_sender_over_stream.h"

namespace roo_transport {
namespace esp_idf {

/// Reliable link transport over an initialized ESP-IDF UART port.
///
/// The caller owns UART configuration and driver installation. `begin()`
/// starts the reliable transport and a background packet receiver.
class ReliableUartLinkTransport {
 public:
  ReliableUartLinkTransport(uart_port_t port, roo::string_view name,
                            LinkBufferSize sendbuf = kBufferSize4KB,
                            LinkBufferSize recvbuf = kBufferSize4KB);

  ReliableUartLinkTransport(const ReliableUartLinkTransport&) = delete;
  ReliableUartLinkTransport& operator=(const ReliableUartLinkTransport&) =
      delete;

  /// Starts the reliable transport and its UART receive thread.
  void begin();

  /// Stops the UART receive thread and the reliable transport.
  void end();

  /// Establishes a new connection and waits for it to complete.
  Link connect(std::function<void()> disconnect_fn = nullptr);

  /// Establishes a new connection without waiting for completion.
  Link connectAsync(std::function<void()> disconnect_fn = nullptr);

  /// Establishes a connection and terminates if the peer later resets.
  Link connectOrDie();

  /// Returns the underlying transport.
  LinkTransport& transport() { return transport_; }

  /// Returns the underlying transport by implicit conversion.
  operator LinkTransport&() { return transport_; }

  /// Returns a stats view for the underlying transport.
  LinkTransport::StatsMonitor statsMonitor() {
    return LinkTransport::StatsMonitor(transport_);
  }

 private:
  roo_io::Esp32UartOutputStream output_;
  roo_io::Esp32UartInputStream input_;
  PacketSenderOverStream sender_;
  PacketReceiverOverStream receiver_;
  LinkTransport transport_;
  std::function<void(const roo::byte*, size_t)> process_fn_;
  std::string receiver_thread_name_;
  roo::thread receiver_thread_;
  roo::atomic<bool> running_{false};
};

}  // namespace esp_idf
}  // namespace roo_transport

#endif  // defined(ESP_PLATFORM) && !defined(ARDUINO)
