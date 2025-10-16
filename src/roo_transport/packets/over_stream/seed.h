#pragma once

#include <cstdint>

namespace roo_transport {

// Arbitrary seed value used in the checksum computation of packets sent over
// the stream. (Incidentally, this is a hash code of an empty packet.)
static const uint32_t kPacketOverStreamSeed = 0xB45DF9DE;

}
