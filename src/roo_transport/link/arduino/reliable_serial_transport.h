#pragma once

#if (defined ARDUINO)

#if (defined ESP32 || defined ROO_TESTING)
#include "roo_transport/link/arduino/esp32/reliable_serial_transport.h"

namespace roo_transport {
using esp32::ReliableSerialTransport;
#if SOC_UART_NUM > 1
using esp32::ReliableSerial1Transport;
#endif  // SOC_UART_NUM > 1
#if SOC_UART_NUM > 2
using esp32::ReliableSerial2Transport;
#endif  // SOC_UART_NUM > 2
}  // namespace roo_transport

#elif defined(ARDUINO_ARCH_RP2040)
#include "roo_transport/link/arduino/rp2040/reliable_serial_transport.h"

namespace roo_transport {
// using rp2040::ReliableSerialTransport;
using rp2040::ReliableSerial1Transport;
using rp2040::ReliableSerial2Transport;
}  // namespace roo_transport

#else

#error "ReliableSerialTransport is not implemented for this platform yet."

#endif

#endif  // (defined ARDUINO)
