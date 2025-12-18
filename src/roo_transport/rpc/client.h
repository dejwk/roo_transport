#pragma once

#include <functional>

#include "roo_collections.h"
#include "roo_collections/flat_small_hash_map.h"
#include "roo_threads.h"
#include "roo_threads/mutex.h"
#include "roo_transport/messaging/messaging.h"
#include "roo_transport/rpc/rpc.h"
#include "roo_transport/rpc/status.h"

namespace roo_transport {

class RpcClient {
 public:
  using UnaryCompletionCb = std::function<void(
      const roo::byte* data, size_t data_size, Status status)>;

  explicit RpcClient(Messaging& messaging);

  Status sendUnaryRpc(RpcFunctionId function_id, const roo::byte* payload,
                      size_t payload_size, UnaryCompletionCb cb);

  Status sendUnaryRpcWithTimeout(RpcFunctionId function_id,
                                 const roo::byte* payload, size_t payload_size,
                                 uint32_t timeout_ms, UnaryCompletionCb cb);

  ~RpcClient() = default;

  void begin();
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

  using OutgoingCalls = 
      roo_collections::FlatSmallHashMap<RpcStreamId, UnaryCompletionCb>;

  // Called when we receive a response from the server. This method dispatches
  // the response to the appropriate result callback.
  void handleResponse(Messaging::ConnectionId connection_id,
                      const roo::byte* data, size_t len);

  // Called when the transport layer detects reconnection, indicating that
  // pending RPCs will never complete (and should thus be failed).
  void connectionReset(Messaging::ConnectionId connection_id);

  RpcStreamId new_stream(RpcClient::UnaryCompletionCb cb);

  Messaging& messaging_;
  Dispatcher dispatcher_;

  roo::mutex mutex_;

  // Guarded by mutex_.
  uint32_t next_stream_id_ = 1;

  OutgoingCalls outgoing_calls_;
};

}  // namespace roo_transport
