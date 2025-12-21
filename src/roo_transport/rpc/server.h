#pragma once

#include <vector>

#include "roo_collections.h"
#include "roo_collections/flat_small_hash_map.h"
#include "roo_threads.h"
#include "roo_threads/mutex.h"
#include "roo_time.h"
#include "roo_transport/messaging/messaging.h"
#include "roo_transport/rpc/internal/server/handler.h"
#include "roo_transport/rpc/serialization.h"
#include "roo_transport/rpc/status.h"

namespace roo_transport {

using FunctionTable =
    roo_collections::FlatSmallHashMap<RpcFunctionId, RpcHandlerFn>;

// Convenience wrapper for implementing synchronous unary RPC handlers.
template <typename Request, typename Response,
          typename RequestDeserializer = Deserializer<Request>,
          typename ResponseSerializer = Serializer<Response>>
class UnaryHandler {
 public:
  using Fn = std::function<Status(const Request&, Response&)>;

  UnaryHandler(Fn fn) : fn_(std::move(fn)) {}

  void operator()(RequestHandle handle, const roo::byte* payload,
                  size_t payload_size, bool fin) const {
    RequestDeserializer deserializer;
    Request req;
    roo_transport::Status status =
        deserializer.deserialize(payload, payload_size, req);
    if (status != roo_transport::kOk) {
      handle.sendFailureResponse(status, "request deserialization failed");
      return;
    }
    Response resp;
    status = fn_(req, resp);
    if (status != roo_transport::kOk) {
      handle.sendFailureResponse(status, "application error");
      return;
    }
    ResponseSerializer serializer;
    auto serialized = serializer.serialize(resp);
    handle.sendSuccessResponse(serialized.data(), serialized.size(), true);
  }

 private:
  Fn fn_;
};

// Convenience wrapper for implementing asynchronous unary RPC handlers.
template <typename Request, typename Response,
          typename RequestDeserializer = Deserializer<Request>,
          typename ResponseSerializer = Serializer<Response>>
class AsyncUnaryHandler {
 public:
  using Fn = std::function<void(const Request&,
                                std::function<void(Status, Response)>)>;

  AsyncUnaryHandler(Fn fn) : fn_(std::move(fn)) {}

  void operator()(RequestHandle handle, const roo::byte* payload,
                  size_t payload_size, bool fin) const {
    RequestDeserializer deserializer;
    Request req;
    roo_transport::Status status =
        deserializer.deserialize(payload, payload_size, req);
    if (status != roo_transport::kOk) {
      handle.sendFailureResponse(status, "request deserialization failed");
      return;
    }
    fn_(req, [handle](Status resp_status, Response resp_val) {
      if (resp_status != roo_transport::kOk) {
        handle.sendFailureResponse(resp_status, "application error");
        return;
      }
      ResponseSerializer serializer;
      auto serialized = serializer.serialize(resp_val);
      handle.sendSuccessResponse(serialized.data(), serialized.size(), true);
    });
  }

 private:
  Fn fn_;
};

class RpcServer {
 public:
  RpcServer(Messaging& messaging, const FunctionTable* function_table);

  void begin();
  void end();

  ~RpcServer() { messaging_.unsetReceiver(); }

 private:
  friend class RequestHandle;

  class Dispatcher : public Messaging::Receiver {
   public:
    explicit Dispatcher(RpcServer& rpc_server) : rpc_server_(rpc_server) {}

    void received(Messaging::ConnectionId connection_id, const roo::byte* data,
                  size_t len) override {
      rpc_server_.handleRequest(connection_id, data, len);
    }

   private:
    RpcServer& rpc_server_;
  };

  void handleRequest(Messaging::ConnectionId connection_id,
                     const roo::byte* data, size_t len);

  void sendSuccessResponse(Messaging::ConnectionId connection_id,
                           RpcStreamId stream_id, const roo::byte* data,
                           size_t len);

  void sendFailureResponse(Messaging::ConnectionId connection_id,
                           RpcStreamId stream_id, Status status,
                           roo::string_view msg);

  // Returns true if the response should be sent; false otherwise. Destroys the
  // request it it has been finished.
  bool prepForResponse(Messaging::ConnectionId connection_id,
                       RpcStreamId stream_id);

  void reconnected();

  Messaging& messaging_;
  Dispatcher dispatcher_;

  const FunctionTable* handlers_;

  Messaging::ConnectionId connection_id_;

  roo::mutex mutex_;

  // Guarded by mutex_.
  roo_collections::FlatSmallHashMap<RpcStreamId, RpcRequest> pending_calls_;
};

}  // namespace roo_transport