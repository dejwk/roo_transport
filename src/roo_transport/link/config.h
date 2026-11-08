#pragma once

// Stack reservation in bytes for transport-owned sender and receive workers.
// Override project-wide with -DROO_TRANSPORT_WORKER_STACK_SIZE=4096, for
// example. Explicit begin(sender_attributes) stack sizes take precedence for
// the sender.
//
// ESP32: 2 KiB passes the tested normal workloads, but crash dumps fail on the
// tested receive-worker FATAL path when CONFIG_ESP_COREDUMP_STACK_SIZE=0 (the
// SDK dumps on the crashing task's stack). Both tested ESP32 revisions rebooted
// with no valid dump at 2 KiB; 4 KiB produced valid dumps. Custom callbacks/log
// sinks can also need more stack.
//
// Arduino-ESP32 owns its UART receive-event task separately. Its default is
// already 2048 bytes; to increase it, also set the Arduino core build flag
// -DARDUINO_SERIAL_EVENT_TASK_STACK_SIZE=4096. The Roo flag cannot resize that
// core-owned task. Apply these flags when compiling the libraries/core too,
// rather than defining them only in a sketch.
#ifndef ROO_TRANSPORT_WORKER_STACK_SIZE
#define ROO_TRANSPORT_WORKER_STACK_SIZE 2048
#endif
