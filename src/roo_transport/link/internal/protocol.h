#pragma once

#include "roo_transport/link/internal/seq_num.h"

namespace roo_transport {
namespace internal {

// Protocol description:
//
// This protocol implements reliable point-to-point bidirectional streaming over
// a lossy packet-based transport. It implements packet deduplication,
// reordering, retransmission, and flow control.
//
// The peers establish the connection by means of a handshake protocol, vaguely
// similar to the TCP handshake protocol. Each side generates a random 32-bit
// stream ID to identify itself. (The ID helps the peer to detect and ignore
// stale packets from previous connections, e.g. after the microcontroller has
// been reset). Each peer sends a handshake 'CONN' packet, including its stream
// ID and a randomly generated initial sequence number. Upon receiving a 'CONN'
// packet, the peer responds by sending a handshake 'CONN/ACK' packet,
// confirming reception of the peer's stream ID and initial sequence number, and
// sending its own stream ID and initial sequence number. The initiator of the
// connection responds by sending a final handshake 'ACK' packet.
//
// Once the handshake is concluded, each side may send and receive data.
// Increasing sequence numbers are used to label the outgoing data packets, and
// to acknowledge reception of incoming data packets. Each side maintains a
// window of incoming and outgoing data packets, to implement flow control and
// to allow for retransmission of lost packets.
//
// The protocol allows the following type of packets:
// * 'handshake', used for the handshake protocol described above;
// * 'data', carrying application data;
// * 'final data', indicating end-of-stream;
// * 'data ack', acknowledging reception of data packets;
// * 'flow control', indicating the maximum sequence number that the recipient
//   has space to receive.
//
// Each packet consists of a 16-bit header, and an optional payload. The format
// of the header is the following:
// * bit 15 (the most significant bit): the 'control' bit;
// * bits 14-12: the packet type (one of the values defined in the PacketType
//   enum below);
// * bits 11-0: the packet's sequence number.
//
// The 'control' bit is used to detect and ignore cross-talk packets (the
// packets originating from our own transmitter, rather than from the peer).
// Cross-talk can happen e.g. when a remote peer gets physically disconnected.
// The two long parallel TX and RX wires, now unconnected at the far end, act as
// a capacitor, and, at high baud rates, can cause electrical coupling between
// the local TX and RX pins.
//
// An easy way to work around cross-talk would be to include the stream ID in
// each packet (similarly to how it is done in IP). However, that would add 4
// bytes of overhead to each packet, which is significant when the packets are
// small. Instead, we use the single 'control' bit to differentiate between the
// two sides of the connection. The rule is that whichever side has the larger
// stream ID sets the control bit in all its outgoing packets; the other side
// clears the control bit in all its outgoing packets. This way, both sides can
// easily identify and ignore cross-talk packets.
//
// Control bit is always zero for handshake packets, since the stream IDs are
// not known yet at that point. During handshake, reception of a CONN packet
// with stream ID and sequence ID identical to our own is interpreted as
// cross-talk. (Probability of that happening by chance is less than 6e-14).
//
// Packet payload formats and semantics:
//
// * 'handshake' packet:
//   Used in the handshake protocol. The payload consists of 9 bytes. The bytes
//   1-4 contain the sender's 32-bit stream ID (in the network order). The bytes
//   5-8 contain the acknowledgement of the previously received peer's 32-bit
//   stream ID (in the network order), or zero if we don't know it yet. In the
//   last byte, the most significant bit indicates whether the sender expects an
//   acknowledgement of the handshake (1 indicates that the ack is requested),
//   and the 4 least significant bits communicate the peer's receive buffer
//   size, as a power of 2 (valid values are 0-12, indicating buffer sizes of
//   1-4096 packets). Remaining bits are reserved and must be zero.
//
// * 'data' packet:
//   the payload is all application data. Must not be empty.
//
// * 'final data' packet:
//   like 'data' packet, but indicates that the sender has no more data to
//   transmit. The recipient's input stream transitions to 'end of stream' after
//   reading this packet. The sender should not send any more packets after this
//   one. The payload may be empty.
//
// * 'data ack' packet:
//   Sent by the recipient, to confirm reception of all packets with sequence
//   numbers preceding the sequence number carried in the header (which we call
//   'unack_seq_number'). The (optional) payload consists of an arbitrary number
//   of bytes, which constitute the the lookahead 'ack' bitmap. That is,
//   successive bits, with most-significant bit first, indicate the 'ack' status
//   of packets with subsequent sequence numbers, starting with unack_seq_number
//   + 1. (Knowing that some packets have been received allows the sender to
//   avoid needless retransmissions).
//
// * 'flow control' packet:
//   Sent by the recipient, to indicate maximum sequence number that the
//   recipient has space to receive.

enum PacketType {
  kDataPacket = 0,
  kFinPacket = 1,
  kDataAckPacket = 2,
  kHandshakePacket = 3,
  kFlowControlPacket = 4,
};

inline bool GetPacketControlBit(uint16_t header) {
  return (header & 0x8000) != 0;
}

inline PacketType GetPacketType(uint16_t header) {
  return (PacketType)((header & 0x7FFF) >> 12);
}

// Control bit differentiates between the two sides of the connection. It is
// determined by whichever side happens to use the larger stream ID.
inline uint16_t FormatPacketHeader(SeqNum seq, PacketType type,
                                   bool control_bit) {
  return (seq.raw() & 0x0FFF) | (type << 12) | (control_bit ? 0x8000 : 0);
}

inline void SetPacketHeaderTypeFin(uint16_t& header) {
  DCHECK_LE(1, header & 0x7000) << "Must be 'data' or 'final data' packet";
  header |= 0x1000;  // Set the 'final' type.
}

}  // namespace internal
}  // namespace roo_transport
