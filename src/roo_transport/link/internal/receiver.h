#pragma once

#include <memory>

#include "roo_transport/link/internal/in_buffer.h"
#include "roo_transport/link/internal/ring_buffer.h"

namespace roo_transport {
namespace internal {

/// Incoming reliable-stream state machine and packet queue.
class Receiver {
 public:
  enum State {
    // Connect was not locally called; no handshake shall be initialized.
    kIdle = 0,

    // Indicates that the connect has been called but we have not yet received
    // the the peer's stream ID and seq.
    kConnecting = 1,

    // Indicates that we received the peer's stream ID and seq, allowing us
    // to receive messages from it.
    kConnected = 2,

    // Indicates that peer has abruptly terminated a previously valid
    // connection.
    kBroken = 3,
  };

  /// Creates a receiver with a buffer of size `1 << recvbuf_log2`.
  Receiver(unsigned int recvbuf_log2);

  /// Returns the current receiver state.
  State state() const { return state_; }
  /// Returns whether end-of-stream has been reached.
  bool eos() const { return end_of_stream_; }

  /// Returns whether the stream has fully completed.
  bool done() const;

  /// Marks the receiver connected to the peer.
  ///
  /// Resets the receive window so packets can be accepted starting at the
  /// advertised peer sequence number.
  void setConnected(SeqNum peer_seq_num, bool control_bit);
  /// Returns the receiver to the idle state.
  void setIdle();
  /// Marks the receiver broken.
  void setBroken();

  /// Reads up to `count` bytes without blocking.
  ///
  /// Consumes packets in order until a gap, EOF marker, or `count` limit is
  /// reached.
  size_t tryRead(roo::byte* buf, size_t count, bool& outgoing_data_ready);

  /// Peeks at the next byte without consuming it.
  int peek();
  /// Returns bytes currently available for immediate reading.
  ///
  /// Returns `1` when an EOF marker is queued so callers can observe end of
  /// stream without consuming payload bytes.
  size_t availableForRead() const;

  /// Resets the receiver to the idle state.
  void reset();
  /// Initializes a new incoming stream.
  void init(uint32_t my_stream_id);

  /// Closes the local input side of the stream.
  ///
  /// Drops any unread buffered data and schedules a flow-control update so the
  /// peer can stop treating that data as outstanding.
  void markInputClosed(bool& outgoing_data_ready);

  /// Serializes an acknowledgment packet into `buf`.
  ///
  /// Includes a skip-ACK bitmap describing later packets that have already
  /// arrived.
  size_t ack(roo::byte* buf);

  /// Serializes a flow-control update into `buf`.
  ///
  /// Periodically re-advertises the receive high-water mark until new traffic
  /// proves the update was observed by the peer.
  size_t updateRecvHimark(roo::byte* buf, long& next_send_micros);

  /// Handles one received data packet.
  ///
  /// Accepts packets inside the receive window, tolerates retransmits of
  /// already seen packets, tracks final-packet state, and requests an ACK when
  /// appropriate.
  bool handleDataPacket(bool control_bit, uint16_t seq_id,
                        const roo::byte* payload, size_t len, bool is_final,
                        bool& has_new_data_to_read);

  /// Returns whether there is no buffered input.
  bool empty() const { return in_ring_.empty(); }

  /// Returns the number of packets received.
  uint32_t packets_received() const { return packets_received_; }

  /// Returns the local stream id.
  uint32_t my_stream_id() const { return my_stream_id_; }

  /// Returns receive buffer capacity as a log2 value.
  unsigned int buffer_size_log2() const { return in_ring_.capacity_log2(); }

 private:
  InBuffer& getInBuffer(SeqNum seq) const {
    return in_buffers_[in_ring_.offset_for(seq)];
  }

  uint32_t my_stream_id_;
  State state_;

  // Set when the input stream is closed on this process, indicating that the
  // reader is not interested in the rest of the data. We will silently read and
  // ignore it.
  bool self_closed_;

  // Set when we receive 'end of stream' notification (kFin packet) from the
  // peer, indicating that there will be no more data to come after that final
  // packet.
  bool peer_closed_;

  // Set when the stream is read till end without error.
  bool end_of_stream_;

  std::unique_ptr<InBuffer[]> in_buffers_;
  mutable InBuffer* current_in_buffer_;
  mutable uint8_t current_in_buffer_pos_;
  RingBuffer in_ring_;

  // Whether we need to send kDataAckPacket.
  bool needs_ack_;

  // Newest unacked seq ID.
  uint16_t unack_seq_;

  // The seq ID past the maximum we're able to receive. We track it on the
  // receiver so that we can decide if it needs to be retransmitted if we
  // don't see new data packets.
  SeqNum recv_himark_;

  // The deadline by which we will (re)send the recv-himark update (by means
  // of a kControlFlotPacket), unless we receive evidence that the up-to-date
  // one was delivered (by means of a data packet with a higher seq). When
  // some receive buffers get freed, we reset it so that the update is sent
  // immediately.
  roo_time::Uptime recv_himark_update_expiration_;

  uint32_t packets_received_;

  bool control_bit_;
};

}  // namespace internal
}  // namespace roo_transport
