#pragma once

#include "roo_transport/link/internal/seq_num.h"

namespace roo_transport {
namespace internal {

// Values are significant; must be 0-15, not change. They are used in the
// communication protocol.
//
// Packet formats:
//
// Generally, each packet consists of a 16-bit header, and some optional
// payload. The topmost 4 bits of the header (in network order) identify the
// packet type (i.e. one of the enum values, below). The remaining 12 bytes
// represent the packet's sequence number.
//
// * kDataPacket:
//   the payload is all application data. Must not be empty.
//
// * kFinalDataPacket:
//   like kDataPacket, but indicates that the sender has no more data to
//   transmit. The recipient's input stream transitions to kEndOfStream after
//   reading this packet. The sender should not send any more packets after this
//   one. The payload may be empty.
//
// * kDataAckPacket:
//   Sent by the recipient, to confirm reception of all packets with sequence
//   numbers preceding the sequence number carried in the header (which we call
//   'unack_seq_number'). The (optional) payload consists of an arbitrary number
//   of bytes, which constitute the the lookahead 'ack' bitmap. That is,
//   successive bits, with most-significant bit first, indicate the 'ack' status
//   of packets with subsequent sequence numbers, starting with unack_seq_number
//   + 1. (Knowing that some packets have been received allows the sender to
//   avoid needless retransmissions).
//
// * kHandshakePacket:
//   Used in the handshake protocol. The payload consists of 9 bytes. The bytes
//   1-4 contain the sender's 32-bit stream ID (in the network order). The bytes
//   5-8 contain the acknowledgement of the previously received peer's 32-bit
//   stream ID (in the network order), or zero if we don't know it yet. The last
//   byte indicates whether the sender expects an acknowledgement of the
//   handshake (a non-zero value indicates that the ack is requested).
//   The handshake protocol is somewhat similar to TCP-IP connection protocol
//   (SYN/SYN-ACK/ACK): the initiating party sends a handshake message including
//   its randomly generated stream ID; the recipient responds with a similar
//   handshake message, acknowledging the sender's stream ID and including its
//   own, and the initiator acknowledges receipt by sending a final handshake
//   message.
//
// * kFlowControlPacket:
//   Sent by the recipient, to indicate maximum sequence number that the
//   recipient has space to receive.flow control packet: 12 bits of header is
//   the maximum number of packets that the receiver still has place to buffer;
//
enum PacketType {
  kDataPacket = 0,
  kFinPacket = 1,
  kDataAckPacket = 2,
  kHandshakePacket = 3,
  kFlowControlPacket = 4,
};

inline PacketType GetPacketType(uint16_t header) {
  return (PacketType)(header >> 12);
}

inline uint16_t FormatPacketHeader(SeqNum seq, PacketType type) {
  return (seq.raw() & 0x0FFF) | (type << 12);
}

}  // namespace internal
}  // namespace roo_transport
