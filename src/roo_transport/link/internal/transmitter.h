#pragma once

#include <memory>

#include "roo_transport/link/internal/out_buffer.h"
#include "roo_transport/link/internal/ring_buffer.h"

namespace roo_transport {
namespace internal {

/// Outgoing reliable-stream state machine and packet queue.
class Transmitter {
 public:
  enum State {
    // Connect was not locally called; no handshake shall be initialized; the
    // send queue is empty.
    kIdle = 0,

    // We initiated the handshake, but the peer has not yet acknowledged receipt
    // of our stream ID. We may buffer data locally, but we should refrain from
    // trying to send it. Send queue is empty.
    kConnecting = 1,

    // Indicates that the peer acknowledged receipt of our stream ID and seq, so
    // that we can be sending messages to it.
    kConnected = 2,

    // Indicates that peer has abruptly terminated a previously valid
    // connection. Send queue is empty.
    kBroken = 3,
  };

  /// Creates a transmitter with a send buffer of size `1 << sendbuf_log2`.
  Transmitter(unsigned int sendbuf_log2);

  /// Resets the transmitter to the idle state.
  void reset();

  /// Initializes a new outgoing stream.
  void init(uint32_t my_stream_id, SeqNum new_start);

  /// Returns the number of sent packets, including retransmissions.
  uint32_t packets_sent() const { return packets_sent_; }

  /// Returns the number of packets confirmed as delivered.
  uint32_t packets_delivered() const { return packets_delivered_; }

  /// Writes up to `count` bytes into the pending packet queue.
  ///
  /// Stops when flow-control credit or free queue slots run out.
  size_t tryWrite(const roo::byte* buf, size_t count, bool& made_space);

  /// Returns the guaranteed writable byte count.
  ///
  /// In the worst case, if the caller flushes after every write, only one byte
  /// is guaranteed per free queue slot.
  size_t availableForWrite() const;

  /// Flushes the current pending packet, if any.
  ///
  /// Marks the partially filled packet ready for transmission even if there is
  /// still payload space left in it.
  bool flush();

  /// Returns whether there is still unacknowledged data pending.
  ///
  /// This reports whether there are any packets left in the send queue.
  bool hasPendingData() const;

  /// Closes the outgoing stream.
  ///
  /// Flushes the current packet and appends an end-of-stream packet when space
  /// is available. If the queue is full, the final packet is deferred until an
  /// ACK frees a slot.
  void close();

  /// Marks the transmitter connected to the peer.
  void setConnected(uint16_t peer_receive_buffer_size, bool control_bit) {
    state_ = kConnected;
    peer_receive_buffer_size_ = peer_receive_buffer_size;
    control_bit_ = control_bit;
    // Update the recv himark to reflect the peer's receive buffer size.
    recv_himark_ = out_ring_.begin() + peer_receive_buffer_size;
  }

  /// Marks the transmitter broken.
  void setBroken();

  /// Returns the current transmitter state.
  State state() const { return state_; }

  /// Returns the local stream id.
  uint32_t my_stream_id() const { return my_stream_id_; }

  /// Returns the next packet buffer to send, if any.
  ///
  /// Prefers in-order first sends before falling back to retransmission
  /// candidates.
  const OutBuffer* getBufferToSend(long& next_send_micros);

  /// Serializes the next outgoing packet into `buf`.
  size_t send(roo::byte* buf, long& next_send_micros);

  /// Returns the first unacknowledged sequence number.
  SeqNum front() const { return out_ring_.begin(); }

  /// Applies an acknowledgment bitmap from the peer.
  ///
  /// Removes fully ACKed packets from the queue and may rush retransmission of
  /// packets that appear to have been skipped.
  bool ack(bool control_bit, uint16_t seq_id, const roo::byte* ack_bitmap,
           size_t ack_bitmap_len);

  /// Updates the peer receive high-water mark.
  ///
  /// Returns `true` when the update increases the send window and therefore may
  /// allow new payload to be queued or transmitted.
  bool updateRecvHimark(bool control_bit, uint16_t recv_himark);

 private:
  OutBuffer& getOutBuffer(SeqNum seq) {
    return out_buffers_[out_ring_.offset_for(seq)];
  }

  void addEosPacket();

  uint32_t my_stream_id_;

  State state_;

  // Set to true by close(). Independent of state, because the writer can write
  // all the input and close the stream even before we manage to establish the
  // connection.
  bool end_of_stream_;

  std::unique_ptr<OutBuffer[]> out_buffers_;
  OutBuffer* current_out_buffer_;
  RingBuffer out_ring_;

  // Pointer used to cycle through packets to send, so that we generally send
  // packets in order before trying any retransmissions.
  SeqNum next_to_send_;

  // Ceiling beyond which the receiver currently isn't able to process data.
  // Used in flow control, stopping the sender from sending more than the
  // receiver can accept. Updated by the receiver by means of
  // kFlowControlPacket.
  SeqNum recv_himark_;

  // Indicates that the sender has closed the stream, but we were unable to
  // update the send queue to reflect that, because it was full. This flag
  // signals that a sentinel termination packet needs to be appended to the
  // output queue at the nearest opportunity.
  bool has_pending_eof_;

  uint32_t packets_sent_;
  uint32_t packets_delivered_;

  // Used to check validity of incoming flow control updates.
  uint16_t peer_receive_buffer_size_;

  // Indicates whether we are the 'control' side of the connection, which is
  // determined by whoever has the larger stream ID.
  // This bit is sent in the packet header to differentiate between the two
  // sides, and to detect cross-talk (e.g. due to wiring mistakes).
  bool control_bit_;
};

}  // namespace internal
}  // namespace roo_transport
