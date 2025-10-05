#include "roo_transport/link/internal/out_buffer.h"

#include "roo_io/memory/load.h"
#include "roo_io/memory/store.h"
#include "roo_transport/link/internal/protocol.h"

#if (defined ESP32 || defined ROO_TESTING)
#include "esp_random.h"
#define RANDOM_INTEGER esp_random
#else
#define RANDOM_INTEGER rand
#endif

namespace roo_transport {
namespace internal {

void OutBuffer::init(SeqNum seq_id) {
  uint16_t header = FormatPacketHeader(seq_id, kDataPacket);
  roo_io::StoreBeU16(header, payload_);
  size_ = 0;
  acked_ = false;
  flushed_ = false;
  finished_ = false;
  expiration_ = roo_time::Uptime::Start();
  send_counter_ = 0;
}

namespace {

roo_time::Duration Backoff(int retry_count) {
  float min_delay_us = 5000.0f;     // 5ms
  float max_delay_us = 200000.0f;  // 200ms
  float delay = pow(1.33, retry_count) * min_delay_us;
  if (delay > max_delay_us) {
    delay = max_delay_us;
  }
  // Randomize by +=20%, to make unrelated retries spread more evenly in time.
  delay += (float)delay * ((float)RANDOM_INTEGER() / RAND_MAX - 0.5f) * 0.4f;
  return roo_time::Micros((uint64_t)delay);
}

}  // namespace

void OutBuffer::markSent(roo_time::Uptime now) {
  if (send_counter_ < 255) ++send_counter_;
  expiration_ = now + Backoff(send_counter_);
}

void OutBuffer::markFinal() {
  SeqNum seq_id(roo_io::LoadBeU16(payload_) & 0x0FFF);
  uint16_t header = FormatPacketHeader(seq_id, kFinPacket);
  roo_io::StoreBeU16(header, payload_);
}

}  // namespace internal
}  // namespace roo_transport