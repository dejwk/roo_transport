#pragma once

#include <memory>

#include "roo_transport/link/internal/out_buffer.h"
#include "roo_transport/link/internal/ring_buffer.h"

namespace roo_transport {
namespace internal {

class Transmitter {
 public:
  enum State {
    // Connect was not locally called; no handshake shall be initialized; the
    // send queue is empty.
    kIdle = 0,

    // We initiated the handshake, but the peer has not yet acknowledged receipt
    // of our stream ID. We may buffer data locally, but we should refrain from
    // trying to send it.
    kConnecting = 1,

    // Indicates that the peer acknowledged receipt of our stream ID and seq, so
    // that we can be sending messages to it.
    kConnected = 2,

    // Indicates that peer has abruptly terminated a previously valid
    // connection.
    kBroken = 3,
  };

  Transmitter(unsigned int sendbuf_log2);

  // Sets the state to kIdle.
  void reset();

  // Sets the state to kConnecting.
  void init(uint32_t my_stream_id, SeqNum new_start);

  uint32_t packets_sent() const { return packets_sent_; }

  uint32_t packets_delivered() const { return packets_delivered_; }

  size_t tryWrite(const roo::byte* buf, size_t count, bool& made_space);
  size_t availableForWrite() const;
  bool flush();

  // Returns true if there are some unacked buffers in the queue.
  bool hasPendingData() const;

  // If connected, sets state to kClosed.
  void close();

  void setConnected() { state_ = kConnected; }
  void setBroken();

  State state() const { return state_; }

  uint32_t my_stream_id() const { return my_stream_id_; }

  const OutBuffer* getBufferToSend(long& next_send_micros);
  size_t send(roo::byte* buf, long& next_send_micros);

  SeqNum front() const { return out_ring_.begin(); }

  // Called when an 'ack' package is received. Removes acked packages from the
  // send queue. Returns true if there is a packet that should be immediately
  // re-delivered, without waiting for its expiration.
  bool ack(uint16_t seq_id, const roo::byte* ack_bitmap, size_t ack_bitmap_len);

  // Returns true if the recv himark has changed, making room for new data to
  // send.
  bool updateRecvHimark(uint16_t recv_himark);

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
};

}  // namespace internal
}  // namespace roo_transport
