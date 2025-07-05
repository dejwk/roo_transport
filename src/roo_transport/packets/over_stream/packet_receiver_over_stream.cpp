#include "roo_transport/packets/over_stream/packet_receiver_over_stream.h"

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_collections.h"
#include "roo_collections/hash.h"
#include "roo_io.h"
#include "roo_io/memory/load.h"
#include "roo_io/third_party/nanocobs/cobs.h"
#include "roo_logging.h"

namespace roo_transport {

PacketReceiverOverStream::PacketReceiverOverStream(roo_io::InputStream& in,
                                                   ReceiverFn receiver_fn)
    : in_(in),
      buf_(new roo::byte[256]),
      tmp_(new roo::byte[256]),
      pos_(0),
      receiver_fn_(std::move(receiver_fn)),
      bytes_received_(0),
      bytes_accepted_(0) {}

void PacketReceiverOverStream::setReceiverFn(ReceiverFn receiver_fn) {
  receiver_fn_ = std::move(receiver_fn);
}

bool PacketReceiverOverStream::tryReceive() {
  size_t len = in_.tryRead(tmp_.get(), 256);
  bytes_received_ += len;
  bool received = false;
  roo::byte* data = &tmp_[0];
  while (len > 0) {
    // Find the possible packet delimiter (zero byte).
    const roo::byte* delim = std::find(data, data + len, roo::byte{0});
    size_t increment = delim - data;
    bool finished = (increment < len);
    if (finished) {
      ++increment;
      if (pos_ + increment <= 256) {
        if (pos_ == 0) {
          processPacket(data, increment);
        } else {
          memcpy(&buf_[pos_], data, increment);
          processPacket(buf_.get(), pos_ + increment);
        }
        received = true;
      }
      pos_ = 0;
    } else {
      if (pos_ + increment < 256) {
        memcpy(&buf_[pos_], data, increment);
        pos_ += increment;
      } else {
        pos_ = 256;
      }
    }
    data += increment;
    len -= increment;

    // Alternative implementation:
    // if (pos_ + increment < 256) {
    //   // Fits within the 'max packet' size.
    //   memcpy(&buf_[pos_], data, increment);
    //   if (finished) {
    //     buf_[pos_ + increment] = 0;
    //     processPacket(buf_.get(), pos_ + increment + 1);
    //     pos_ = 0;
    //     // Skip the zero byte itself.
    //     increment++;
    //   } else {
    //     pos_ += increment;
    //   }
    // } else {
    //   // Ignore all bytes up to the next packet.
    //   if (finished) {
    //     pos_ = 0;
    //     increment++;
    //   } else {
    //     pos_ = 256;
    //   }
    // }
    // data += increment;
    // len -= increment;
  }
  return received;
}

void PacketReceiverOverStream::processPacket(roo::byte* buf, size_t size) {
  if (cobs_decode_tinyframe(buf, size) != COBS_RET_SUCCESS) {
    // Invalid payload (COBS decoding failed). Dropping packet.
    return;
  }
  {
    // Verify the checksum.
    uint32_t computed_hash = roo_collections::murmur3_32(&buf[1], size - 6, 0);
    uint32_t received_hash = roo_io::LoadBeU32(&buf[size - 5]);
    if (computed_hash != received_hash) {
      // Invalid checksum. Dropping packet.
      return;
    }
    bytes_accepted_ += size;
  }
  if (receiver_fn_ != nullptr) receiver_fn_(&buf[1], size - 6);
}

}  // namespace roo_transport