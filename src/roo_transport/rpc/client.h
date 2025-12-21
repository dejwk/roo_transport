#pragma once

#include <functional>

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

// Convenience wrapper for implementing unary RPC stubs.
template <typename Request, typename Response,
          typename RequestSerializer = Serializer<Request>,
          typename ResponseDeserializer = Deserializer<Response>>
class UnaryStub {
 public:
  UnaryStub(RpcClient& client, RpcFunctionId function_id)
      : client_(client), function_id_(function_id) {}

  roo_transport::Status call(const Request& request, Response& response) {
    roo::latch completed(1);
    RequestSerializer serializer;
    // Serialize the request message.
    auto serialized = serializer.serialize(request);
    // Bail in case the argument serialization failed.
    if (serialized.status() != kOk) {
      return serialized.status();
    }
    roo_transport::Status status;
    roo_transport::Status req_status = client_.sendUnaryRpc(
        function_id_, serialized.data(), serialized.size(),
        [&completed, &response, &status](const roo::byte* data, size_t len,
                                         roo_transport::Status resp_status) {
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

  roo_transport::Status callAsync(
      const Request& request,
      std::function<void(roo_transport::Status, Response)> completion_cb) {
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
                        roo_transport::Status resp_status) {
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
