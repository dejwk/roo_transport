#include "roo_transport/link/esp_idf/reliable_uart.h"

#if defined(ESP_PLATFORM) && !defined(ARDUINO)

#include <utility>

#include "roo_time.h"

namespace roo_transport {
namespace esp_idf {

ReliableUartLinkTransport::ReliableUartLinkTransport(uart_port_t port,
                                                     roo::string_view name,
                                                     LinkBufferSize sendbuf,
                                                     LinkBufferSize recvbuf)
    : output_(port),
      input_(port),
      sender_(output_),
      receiver_(input_),
      transport_(sender_, name, sendbuf, recvbuf),
      process_fn_([this](const roo::byte* data, size_t size) {
        transport_.processIncomingPacket(data, size);
      }),
      receiver_thread_name_(name.data(), name.size()) {
  receiver_thread_name_ += "-receive";
}

void ReliableUartLinkTransport::begin() {
  transport_.begin();
  running_ = true;
  roo::thread::attributes attrs;
  attrs.set_name(receiver_thread_name_.c_str());
  attrs.set_stack_size(4096);
  receiver_thread_ = roo::thread(attrs, [this]() {
    while (running_) {
      if (receiver_.tryReceive(process_fn_) == 0) {
        roo::this_thread::sleep_for(roo_time::Millis(1));
      }
    }
  });
}

void ReliableUartLinkTransport::end() {
  running_ = false;
  receiver_thread_.join();
  transport_.end();
}

Link ReliableUartLinkTransport::connectAsync(
    std::function<void()> disconnect_fn) {
  return transport_.connectAsync(std::move(disconnect_fn));
}

Link ReliableUartLinkTransport::connect(std::function<void()> disconnect_fn) {
  return transport_.connect(std::move(disconnect_fn));
}

Link ReliableUartLinkTransport::connectOrDie() {
  return transport_.connectOrDie();
}

}  // namespace esp_idf
}  // namespace roo_transport

#endif  // defined(ESP_PLATFORM) && !defined(ARDUINO)
