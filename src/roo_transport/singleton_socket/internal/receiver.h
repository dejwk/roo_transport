#pragma once

#include <memory>

#include "roo_transport/singleton_socket/internal/in_buffer.h"
#include "roo_transport/singleton_socket/internal/ring_buffer.h"

namespace roo_transport {
namespace internal {

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

  Receiver(unsigned int recvbuf_log2);

  State state() const { return state_; }
  bool eos() const { return end_of_stream_; }

  bool done() const;

  void setConnected(SeqNum peer_seq_num);
  void setIdle();
  void setBroken();

  size_t tryRead(roo::byte* buf, size_t count, bool& outgoing_data_ready);

  int peek();
  size_t availableForRead() const;

  void reset();
  void init(uint32_t my_stream_id);

  void markInputClosed(bool& outgoing_data_ready);

  size_t ack(roo::byte* buf);
  size_t updateRecvHimark(roo::byte* buf, long& next_send_micros);

  bool handleDataPacket(uint16_t seq_id, const roo::byte* payload, size_t len,
                        bool is_final, bool& has_new_data_to_read);

  bool empty() const { return in_ring_.empty(); }

  uint32_t packets_received() const { return packets_received_; }

  uint32_t my_stream_id() const { return my_stream_id_; }

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
};

}  // namespace internal
}  // namespace roo_transport
