#pragma once

#include <atomic>
#include <memory>

#include "roo_transport/link/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_io/core/input_stream.h"
#include "roo_io/core/output_stream.h"
#include "roo_io/memory/load.h"
#include "roo_io/memory/store.h"
#include "roo_logging.h"
#include "roo_threads.h"
#include "roo_threads/mutex.h"
#include "roo_threads/thread.h"
#include "roo_transport/link/internal/in_buffer.h"
#include "roo_transport/link/internal/out_buffer.h"
#include "roo_transport/link/internal/receiver.h"
#include "roo_transport/link/internal/ring_buffer.h"
#include "roo_transport/link/internal/seq_num.h"
#include "roo_transport/link/internal/thread_safe/outgoing_data_ready_notification.h"
#include "roo_transport/link/internal/thread_safe/thread_safe_receiver.h"
#include "roo_transport/link/internal/thread_safe/thread_safe_transmitter.h"
#include "roo_transport/link/internal/transmitter.h"
#include "roo_transport/link/link_buffer_size.h"
#include "roo_transport/link/link_status.h"
#include "roo_transport/packets/packet_receiver.h"
#include "roo_transport/packets/packet_sender.h"

namespace roo_transport {

// Helper class to implement reliable bidirectional streaming over lossy
// packet-based transport. Used as a building block of SingletonSerial.
class Channel {
 public:
  Channel(PacketSender& sender, LinkBufferSize sendbuf, LinkBufferSize recvbuf);

  ~Channel();

  void begin();
  void end();

  uint32_t my_stream_id() const;

  size_t write(const roo::byte* buf, size_t count, uint32_t my_stream_id,
               roo_io::Status& stream_status);

  size_t tryWrite(const roo::byte* buf, size_t count, uint32_t my_stream_id,
                  roo_io::Status& stream_status);

  size_t read(roo::byte* buf, size_t count, uint32_t my_stream_id,
              roo_io::Status& stream_status);

  size_t tryRead(roo::byte* buf, size_t count, uint32_t my_stream_id,
                 roo_io::Status& stream_status);

  // Returns -1 if no data available to read immediately.
  int peek(uint32_t my_stream_id, roo_io::Status& stream_status);

  size_t availableForRead(uint32_t my_stream_id,
                          roo_io::Status& stream_status) const;

  void flush(uint32_t my_stream_id, roo_io::Status& stream_status);

  void close(uint32_t my_stream_id, roo_io::Status& stream_status);

  void closeInput(uint32_t my_stream_id, roo_io::Status& stream_status);

  // Returns the delay, in microseconds, until we're expected to need to
  // (re)send the next packet.
  long trySend();

  void packetReceived(const roo::byte* buf, size_t len);

  void disconnect(uint32_t my_stream_id);

  // The lower bound of bytes that are guaranteed to be writable without
  // blocking.
  size_t availableForWrite(uint32_t my_stream_id,
                           roo_io::Status& stream_status) const;

  uint32_t packets_sent() const { return transmitter_.packets_sent(); }

  uint32_t packets_delivered() const {
    return transmitter_.packets_delivered();
  }

  uint32_t packets_received() const { return receiver_.packets_received(); }

  // Returns a newly-generated my_stream_id.
  uint32_t connect(std::function<void()> disconnect_fn = nullptr);

  LinkStatus getLinkStatus(uint32_t my_stream_id);

  void awaitConnected(uint32_t my_stream_id);
  bool awaitConnected(uint32_t my_stream_id, roo_time::Duration timeout);

 private:
  friend class SenderThread;

#ifdef ROO_USE_THREADS

  friend void SendLoop(Channel* retransmitter);

#endif

  LinkStatus getLinkStatusInternal(uint32_t my_stream_id);

  void handleHandshakePacket(uint16_t peer_seq_num, uint32_t peer_stream_id,
                             uint32_t ack_stream_id, bool want_ack,
                             bool& outgoing_data_ready);

  size_t conn(roo::byte* buf, long& next_send_micros);

  void sendLoop();

  PacketSender& packet_sender_;

  // Signals the sender thread that there are packets to send.
  internal::OutgoingDataReadyNotification outgoing_data_ready_;

  internal::ThreadSafeTransmitter transmitter_;
  internal::ThreadSafeReceiver receiver_;

  // Random-generated; used in connect packets.
  // GUARDED_BY(handshake_mutex_).
  uint32_t my_stream_id_;

  // Effectively says that the transmitter is connected to the peer.
  // GUARDED_BY(handshake_mutex_).
  bool my_stream_id_acked_by_peer_;

  // As received from the peer in their connect packets.
  // GUARDED_BY(handshake_mutex_).
  uint32_t peer_stream_id_;

  // Indicates whether we're expected to send the handshake ack message.
  // GUARDED_BY(handshake_mutex_).
  bool needs_handshake_ack_;

  // Used in the handshake backoff protocol.
  // GUARDED_BY(handshake_mutex_).
  uint32_t successive_handshake_retries_;

  // GUARDED_BY(handshake_mutex_).
  roo_time::Uptime next_scheduled_handshake_update_;

  // If not null, will be called, exactly once (from the receive thread) as soon
  // as disconnection is detected.
  // GUARDED_BY(handshake_mutex_).
  std::function<void()> disconnect_fn_;

#ifdef ROO_USE_THREADS
  roo::thread sender_thread_;
  std::atomic<bool> active_;

  mutable roo::mutex handshake_mutex_;

  roo::condition_variable connected_cv_;
#endif
};

}  // namespace roo_transport

#endif  // ROO_USE_THREADS
