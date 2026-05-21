#pragma once

#include <memory>

#include "roo_transport/link/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_backport/string_view.h"
#include "roo_io/core/input_stream.h"
#include "roo_io/core/output_stream.h"
#include "roo_io/memory/load.h"
#include "roo_io/memory/store.h"
#include "roo_logging.h"
#include "roo_threads.h"
#include "roo_threads/atomic.h"
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

/// Reliable bidirectional byte-stream channel over a packet transport.
class Channel {
 public:
  /// Creates a channel over `sender` with the requested buffer sizes.
  ///
  /// `name` is used only for diagnostic logging and sender-thread naming.
  Channel(PacketSender& sender, LinkBufferSize sendbuf, LinkBufferSize recvbuf,
          roo::string_view name = "");

  /// Stops the sender thread and releases channel resources.
  ~Channel();

  /// Starts the sender thread.
  void begin();
  /// Stops the sender thread.
  void end();

  /// Returns the currently active local stream id.
  ///
  /// Returns zero while disconnected.
  uint32_t my_stream_id() const;

  /// Writes up to `count` bytes, blocking if needed.
  ///
  /// Fails through `stream_status` if `my_stream_id` no longer matches the
  /// active connection.
  size_t write(const roo::byte* buf, size_t count, uint32_t my_stream_id,
               roo_io::Status& stream_status);

  /// Writes up to `count` bytes without blocking.
  ///
  /// Fails through `stream_status` if `my_stream_id` no longer matches the
  /// active connection.
  size_t tryWrite(const roo::byte* buf, size_t count, uint32_t my_stream_id,
                  roo_io::Status& stream_status);

  /// Reads up to `count` bytes, blocking if needed.
  ///
  /// Fails through `stream_status` if `my_stream_id` no longer matches the
  /// active connection.
  size_t read(roo::byte* buf, size_t count, uint32_t my_stream_id,
              roo_io::Status& stream_status);

  /// Reads up to `count` bytes without blocking.
  ///
  /// Fails through `stream_status` if `my_stream_id` no longer matches the
  /// active connection.
  size_t tryRead(roo::byte* buf, size_t count, uint32_t my_stream_id,
                 roo_io::Status& stream_status);

  /// Peeks at the next readable byte, or `-1` if none is available.
  int peek(uint32_t my_stream_id, roo_io::Status& stream_status);

  /// Returns bytes currently available for immediate reading.
  size_t availableForRead(uint32_t my_stream_id,
                          roo_io::Status& stream_status) const;

  /// Flushes pending outbound data for the stream.
  ///
  /// Marks the current partially filled packet ready for send.
  void flush(uint32_t my_stream_id, roo_io::Status& stream_status);

  /// Closes the outbound side of the stream.
  ///
  /// Ensures an end-of-stream packet is eventually emitted once queued data is
  /// drained.
  void close(uint32_t my_stream_id, roo_io::Status& stream_status);

  /// Closes the inbound side of the stream.
  ///
  /// Drops unread input and notifies the peer via flow-control updates.
  void closeInput(uint32_t my_stream_id, roo_io::Status& stream_status);

  /// Performs one send pass and returns when more send work is due.
  ///
  /// The pass may emit handshake packets, ACKs, flow-control packets, or data
  /// packets, depending on channel state.
  long trySend();

  /// Passes one received packet into the channel.
  ///
  /// The packet may affect handshake state, ACK processing, flow control, or
  /// buffered payload delivery.
  void packetReceived(const roo::byte* buf, size_t len);

  /// Disconnects the stream if `my_stream_id` still matches the active link.
  ///
  /// If a disconnect callback was registered for that stream, it is invoked
  /// exactly once.
  void disconnect(uint32_t my_stream_id);

  /// Returns the guaranteed writable byte count.
  size_t availableForWrite(uint32_t my_stream_id,
                           roo_io::Status& stream_status) const;

  /// Returns the number of sent packets, including retransmissions.
  uint32_t packets_sent() const { return transmitter_.packets_sent(); }

  /// Returns the number of packets confirmed as delivered.
  uint32_t packets_delivered() const {
    return transmitter_.packets_delivered();
  }

  /// Returns the number of packets received.
  uint32_t packets_received() const { return receiver_.packets_received(); }

  /// Starts connecting and returns the new local stream id.
  ///
  /// Resets any previous stream state, generates a fresh non-zero stream id,
  /// starts the handshake, and installs `disconnect_fn` as the callback for
  /// later disconnect detection.
  uint32_t connect(std::function<void()> disconnect_fn = nullptr);

  /// Returns the current status of the stream identified by `my_stream_id`.
  LinkStatus getLinkStatus(uint32_t my_stream_id);

  /// Waits until a connecting stream becomes connected or broken.
  ///
  /// Returns immediately for idle, connected, or broken streams.
  void awaitConnected(uint32_t my_stream_id);

  /// Waits for the stream state to change or for `timeout` to elapse.
  ///
  /// Returns `true` if the stream stops being `kConnecting`, or `false` if the
  /// timeout expires first.
  bool awaitConnected(uint32_t my_stream_id, roo_time::Duration timeout);

 private:
  friend class SenderThread;

#ifdef ROO_USE_THREADS

  friend void SendLoop(Channel* retransmitter);

#endif

  LinkStatus getLinkStatusInternal(uint32_t my_stream_id);

  void handleHandshakePacket(uint16_t peer_seq_num, uint32_t peer_stream_id,
                             uint32_t ack_stream_id, bool want_ack,
                             uint16_t peer_receive_buffer_size,
                             bool& outgoing_data_ready);

  size_t conn(roo::byte* buf, long& next_send_micros);

  void sendLoop();

  roo::string_view getLogPrefix() const { return log_prefix_; }

  bool my_control_bit() const { return my_stream_id_ > peer_stream_id_; }

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
  roo::atomic<bool> active_;

  mutable roo::mutex handshake_mutex_;

  roo::condition_variable connected_cv_;
#endif

  std::string log_prefix_;
  std::string send_thread_name_;
};

}  // namespace roo_transport

#endif  // ROO_USE_THREADS
