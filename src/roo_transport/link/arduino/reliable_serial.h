#pragma once

#if (defined ARDUINO)
#if (defined ESP32 || defined ROO_TESTING)

#include "roo_transport/link/arduino/esp32/reliable_serial.h"

namespace roo_transport {

using ReliableSerial = esp32::ReliableSerial;

#if SOC_UART_NUM > 1
using ReliableSerial1 = esp32::ReliableSerial1;
#endif  // SOC_UART_NUM > 1
#if SOC_UART_NUM > 2
using ReliableSerial2 = esp32::ReliableSerial2;
#endif  // SOC_UART_NUM > 2

}  // namespace roo_transport

#elif defined(ARDUINO_ARCH_RP2040)

#include "roo_transport/link/arduino/rp2040/reliable_serial.h"

namespace roo_transport {
// using rp2040::ReliableSerial;
using ReliableSerial1 = rp2040::ReliableSerial1;
using ReliableSerial2 = rp2040::ReliableSerial2;
}  // namespace roo_transport

#endif

#endif  // defined(ARDUINO)