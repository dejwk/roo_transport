#pragma once

#include <functional>
#include <memory>

#include "roo_collections.h"
#include "roo_collections/flat_small_hash_map.h"
#include "roo_threads.h"
#include "roo_threads/latch.h"
#include "roo_threads/mutex.h"
#include "roo_transport/messaging/messaging.h"
#include "roo_transport/rpc/rpc.h"
#include "roo_transport/rpc/serialization.h"
#include "roo_transport/rpc/status.h"

namespace roo_transport {

/// Client-side helper for issuing unary RPCs over a `Messaging` transport.
class RpcClient {
 public:
  /// Completion callback used for unary RPC responses.
  ///
  /// `data` and `data_size` describe the response payload after the RPC header
  /// has been stripped. `status` is the transport or server status associated
  /// with the completed call. When the underlying connection resets before a
  /// pending call completes, the callback is invoked with `nullptr`, zero
  /// length, and `kUnavailable`.
  using UnaryCompletionCb = std::function<void(
      const roo::byte* data, size_t data_size, RpcStatus status)>;

  /// Creates a client that uses `messaging` for request and response traffic.
  explicit RpcClient(Messaging& messaging);

  /// Sends a unary RPC request without a timeout.
  ///
  /// Allocates a new stream id, stores `cb` as the completion handler for that
  /// stream, prepends a unary-request RPC header, and hands the message to the
  /// underlying `Messaging` transport.
  ///
  /// @return `kOk` if the request was accepted for send, or `kUnavailable` if
  /// the underlying messaging transport rejected it.
  RpcStatus sendUnaryRpc(RpcFunctionId function_id, const roo::byte* payload,
                         size_t payload_size, UnaryCompletionCb cb);

  /// Sends a unary RPC request with a server-side timeout.
  ///
  /// Behaves like `sendUnaryRpc()`, but encodes `timeout_ms` into the RPC
  /// request header so the server can fail the call after the specified
  /// deadline. The timeout starts when the server receives the request; it
  /// does not bound connection setup or a blocked send. Expiry sends
  /// `kDeadlineExceeded` but does not interrupt application handler code.
  ///
  /// @return `kOk` if the request was accepted for send, or `kUnavailable` if
  /// the underlying messaging transport rejected it.
  RpcStatus sendUnaryRpcWithTimeout(RpcFunctionId function_id,
                                    const roo::byte* payload,
                                    size_t payload_size, uint32_t timeout_ms,
                                    UnaryCompletionCb cb);

  /// Destroys the client.
  ~RpcClient() = default;

  /// Registers the response dispatcher with the underlying messaging layer.
  ///
  /// Call this before expecting incoming RPC responses.
  void begin();

  /// Unregisters the response dispatcher from the messaging layer.
  ///
  /// After this returns, incoming transport messages are no longer routed to
  /// this client.
  void end();

 private:
  class Dispatcher : public Messaging::Receiver {
   public:
    explicit Dispatcher(RpcClient& rpc_client) : rpc_client_(rpc_client) {}

    void received(Messaging::ConnectionId connection_id, const roo::byte* data,
                  size_t len) override {
      rpc_client_.handleResponse(connection_id, data, len);
    }

    void reset(Messaging::ConnectionId connection_id) override {
      rpc_client_.connectionReset(connection_id);
    }

   private:
    RpcClient& rpc_client_;
  };

  // A send failure must also synchronize with callbacks already claimed by
  // a concurrent response/reset, before the caller can destroy its captures.
  struct OutgoingCall {
    explicit OutgoingCall(UnaryCompletionCb cb) : cb(std::move(cb)) {}
    roo::mutex mutex;
    UnaryCompletionCb cb;
  };
  using OutgoingCalls = roo_collections::FlatSmallHashMap<
      RpcStreamId, std::shared_ptr<OutgoingCall>>;

  void cancelSend(RpcStreamId stream_id,
                  const std::shared_ptr<OutgoingCall>& call);
  static void complete(const std::shared_ptr<OutgoingCall>& call,
                       const roo::byte* data, size_t len, RpcStatus status);

  // Called when we receive a response from the server. This method dispatches
  // the response to the appropriate result callback.
  void handleResponse(Messaging::ConnectionId connection_id,
                      const roo::byte* data, size_t len);

  // Called when the transport layer detects reconnection, indicating that
  // pending RPCs will never complete (and should thus be failed).
  void connectionReset(Messaging::ConnectionId connection_id);

  RpcStreamId new_stream(const std::shared_ptr<OutgoingCall>& call);

  Messaging& messaging_;
  Dispatcher dispatcher_;

  roo::mutex mutex_;

  // Guarded by mutex_.
  uint32_t next_stream_id_ = 1;

  OutgoingCalls outgoing_calls_;
};

/// Typed convenience wrapper for invoking one unary RPC function.
template <typename Request, typename Response,
          typename RequestSerializer = Serializer<Request>,
          typename ResponseDeserializer = Deserializer<Response>>
class UnaryStub {
 public:
  /// Creates a stub bound to `function_id` on `client`.
  UnaryStub(RpcClient& client, RpcFunctionId function_id)
      : client_(client), function_id_(function_id) {}

  /// Calls the RPC synchronously and waits for the response.
  ///
  /// Serializes `request`, submits it through the client, blocks on a latch
  /// until the completion callback runs, and deserializes the response payload
  /// into `response` when the RPC status is `kOk`.
  RpcStatus call(const Request& request, Response& response) {
    roo::latch completed(1);
    RequestSerializer serializer;
    // Serialize the request message.
    auto serialized = serializer.serialize(request);
    // Bail in case the argument serialization failed.
    if (serialized.status() != kOk) {
      return serialized.status();
    }
    RpcStatus status;
    RpcStatus req_status = client_.sendUnaryRpc(
        function_id_, serialized.data(), serialized.size(),
        [&completed, &response, &status](const roo::byte* data, size_t len,
                                         RpcStatus resp_status) {
          ResponseDeserializer deserializer;
          if (resp_status == kOk) {
            resp_status = deserializer.deserialize((const roo_io::byte*)data,
                                                   len, response);
          }
          status = resp_status;
          completed.count_down();
        });
    if (req_status != kOk) {
      return req_status;
    }
    completed.wait();
    return status;
  }

  /// Calls the RPC asynchronously and invokes `completion_cb` on completion.
  ///
  /// Serializes `request`, submits it through the client, and later
  /// deserializes the response before invoking `completion_cb`. If request
  /// serialization fails, the call returns that status immediately and no RPC
  /// is sent.
  RpcStatus callAsync(const Request& request,
                      std::function<void(RpcStatus, Response)> completion_cb) {
    RequestSerializer serializer;
    // Serialize the request message.
    auto serialized = serializer.serialize(request);
    // Bail in case the argument serialization failed.
    if (serialized.status() != kOk) {
      return serialized.status();
    }
    return client_.sendUnaryRpc(
        function_id_, serialized.data(), serialized.size(),
        [completion_cb](const roo::byte* data, size_t len,
                        RpcStatus resp_status) {
          ResponseDeserializer deserializer;
          Response resp;
          if (resp_status == kOk) {
            resp_status =
                deserializer.deserialize((const roo_io::byte*)data, len, resp);
          }
          completion_cb(resp_status, std::move(resp));
        });
  }

 private:
  RpcClient& client_;
  RpcFunctionId function_id_;
};

}  // namespace roo_transport
