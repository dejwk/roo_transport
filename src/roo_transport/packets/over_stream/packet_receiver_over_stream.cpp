#include "roo_transport/packets/over_stream/packet_receiver_over_stream.h"

#include <algorithm>

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_collections.h"
#include "roo_collections/hash.h"
#include "roo_io.h"
#include "roo_io/memory/load.h"
#include "roo_io/third_party/nanocobs/cobs.h"
#include "roo_logging.h"
#include "roo_transport/packets/over_stream/seed.h"

namespace roo_transport {

PacketReceiverOverStream::PacketReceiverOverStream(roo_io::InputStream& in)
    : in_(in),
      buf_(new roo::byte[256]),
      tmp_(new roo::byte[256]),
      pos_(0),
      bytes_received_(0),
      bytes_accepted_(0) {}

size_t PacketReceiverOverStream::receive(const ReceiverFn& receiver_fn) {
  while (true) {
    size_t len = in_.read(tmp_.get(), 256);
    if (len == 0) return 0;
    size_t packets = processIncoming(len, receiver_fn);
    if (packets > 0) return packets;
  }
}

size_t PacketReceiverOverStream::tryReceive(const ReceiverFn& receiver_fn) {
  size_t len = in_.tryRead(tmp_.get(), 256);
  return processIncoming(len, receiver_fn);
}

size_t PacketReceiverOverStream::processIncoming(
    size_t len, const ReceiverFn& receiver_fn) {
  bytes_received_ += len;
  size_t received = 0;
  roo::byte* data = &tmp_[0];
  while (len > 0) {
    // Find the possible packet delimiter (zero byte).
    const roo::byte* delim = std::find(data, data + len, roo::byte{0});
    size_t increment = delim - data;
    bool finished = (increment < len);
    if (finished) {
      ++increment;
      if (pos_ + increment <= 256 && pos_ + increment >= 6) {
        // Packet is of an acceptable size.
        if (pos_ == 0) {
          // Fast path: the entire packet fits within the buffer, and we have no
          // partial packet pending. Process it directly from the temporary
          // buffer.
          received += (processPacket(data, increment, receiver_fn) ? 1 : 0);
        } else {
          memcpy(&buf_[pos_], data, increment);
          received +=
              (processPacket(buf_.get(), pos_ + increment, receiver_fn) ? 1
                                                                        : 0);
        }
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
    // if (pos_ + increment <= 256 && pos_ + increment >= 6) {
    //   // Packet is of an acceptable size.
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

bool PacketReceiverOverStream::processPacket(roo::byte* buf, size_t size,
                                             const ReceiverFn& receiver_fn) {
  if (cobs_decode_tinyframe(buf, size) != COBS_RET_SUCCESS) {
    // Invalid payload (COBS decoding failed). Dropping packet.
    return false;
  }
  {
    // Verify the checksum.
    uint32_t computed_hash =
        roo_collections::murmur3_32(&buf[1], size - 6, kPacketOverStreamSeed);
    uint32_t received_hash = roo_io::LoadBeU32(&buf[size - 5]);
    if (computed_hash != received_hash) {
      // Invalid checksum. Dropping packet.
      return false;
    }
    bytes_accepted_ += size;
  }
  if (receiver_fn != nullptr) receiver_fn(&buf[1], size - 6);
  return true;
}

}  // namespace roo_transport