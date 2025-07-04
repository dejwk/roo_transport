#include "roo_transport/packets/over_stream/packet_sender_over_stream.h"

#include "roo_collections/hash.h"
#include "roo_io/memory/store.h"
#include "roo_io/third_party/nanocobs/cobs.h"
#include "roo_logging.h"

namespace roo_io {

PacketSenderOverStream::PacketSenderOverStream(OutputStream& out)
    : out_(out), buf_(new byte[256]) {}

void PacketSenderOverStream::send(const roo::byte* buf, size_t len) {
  // We will use 4 bytes for checksum, and 2 bytes for COBS overhead.
  CHECK_LE(len, kMaxPacketSize);
  buf_[0] = (roo::byte)COBS_TINYFRAME_SENTINEL_VALUE;
  memcpy(&buf_[1], buf, len);
  uint32_t hash = roo_collections::murmur3_32(&buf_[1], len, 0);
  roo_io::StoreBeU32(hash, &buf_[len + 1]);
  buf_[len + 5] = (roo::byte)COBS_TINYFRAME_SENTINEL_VALUE;
  CHECK_EQ(COBS_RET_SUCCESS, cobs_encode_tinyframe(buf_.get(), len + 6));
  out_.writeFully(buf_.get(), len + 6);
}

}  // namespace roo_io