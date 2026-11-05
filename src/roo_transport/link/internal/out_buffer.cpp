#include "roo_transport/link/internal/out_buffer.h"

#include <cstdlib>

#include "roo_io/memory/load.h"
#include "roo_io/memory/store.h"
#include "roo_transport/link/internal/protocol.h"
#include "roo_transport/link/internal/retry_backoff.h"

#if (defined ESP32 || defined ROO_TESTING)
#include "esp_random.h"
#define RANDOM_INTEGER esp_random
#define RANDOM_INTEGER_MAX UINT32_MAX
#else
#define RANDOM_INTEGER rand
#define RANDOM_INTEGER_MAX RAND_MAX
#endif

namespace roo_transport {
namespace internal {

void OutBuffer::init(SeqNum seq_id, bool control_bit) {
  uint16_t header = FormatPacketHeader(seq_id, kDataPacket, control_bit);
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
  return roo_time::Micros(RetryBackoffMicros<RANDOM_INTEGER_MAX>(
      5000, 200000, retry_count, RANDOM_INTEGER()));
}

}  // namespace

void OutBuffer::markSent(roo_time::Uptime now) {
  if (send_counter_ < 255) ++send_counter_;
  expiration_ = now + Backoff(send_counter_);
}

void OutBuffer::markFinal() {
  uint16_t raw = roo_io::LoadBeU16(payload_);
  SetPacketHeaderTypeFin(raw);
  roo_io::StoreBeU16(raw, payload_);
}

}  // namespace internal
}  // namespace roo_transport