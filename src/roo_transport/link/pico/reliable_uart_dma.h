#pragma once

#if defined(ARDUINO_ARCH_RP2040) && defined(__FREERTOS) && !defined(ROO_TESTING)

#include <hardware/uart.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include "roo_transport/link/link_transport.h"

namespace roo_transport {
namespace pico {

/// Reliable UART transport using native RP2040 DMA and a receive worker.
///
/// Borrows an initialized SDK UART exclusively; do not use Arduino Serial1/2 or
/// another driver on that UART. Configure its pins, framing and baud before
/// construction. Uses the normal roo_transport framing and reliability
/// protocol. Requires the roo_threads FreeRTOS backend. Native hardware only;
/// the host emulator does not currently model DMA.
///
/// Construction and destruction must be serialized on one core, shared by all
/// instances, because the DMA completion interrupt is installed on that core.
/// Stop application I/O and destroy returned Links before end()/destruction.
class ReliableUartDma {
 public:
  /// RX ring capacity and required alignment, in bytes.
  static constexpr size_t kRxBufferSize = 8192;

  /// Creates a transport borrowing @p uart and an aligned @p rx_buffer.
  ///
  /// @p rx_buffer must contain kRxBufferSize bytes, be aligned to that size,
  /// and outlive this object. Claims two DMA channels, a hardware spinlock and
  /// an 8 KiB TX ring. Resource exhaustion is fatal. Both windows are
  /// configurable independently of the DMA rings. Does not change any clock or
  /// UART baud.
  ReliableUartDma(uart_inst_t* uart, uint8_t* rx_buffer,
                  LinkBufferSize sendbuf = kBufferSize16KB,
                  LinkBufferSize recvbuf = kBufferSize16KB);

  /// Stops workers, drains TX and releases owned DMA resources.
  ~ReliableUartDma();

  ReliableUartDma(const ReliableUartDma&) = delete;
  ReliableUartDma& operator=(const ReliableUartDma&) = delete;

  /// Starts the sender and receive workers; call once before connecting.
  void begin();

  /// Stops workers, drains TX and releases DMA; this instance cannot restart.
  /// Must run on the construction core after application I/O has stopped.
  void end();

  /// Returns the transport for connecting, reading/writing and monitoring
  /// stats.
  LinkTransport& transport();

  /// Returns the number of detected RX ring overruns, cumulatively.
  /// Overruns discard overwritten bytes; the reliable protocol resynchronizes
  /// and retransmits them. Read after quiescing traffic for final accounting.
  uint32_t receiveOverruns() const;

  /// Returns the number of observed UART receive-error indications.
  /// Includes hardware FIFO overflow, framing, parity and break indications.
  uint32_t receiveErrors() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace pico
}  // namespace roo_transport
#endif
