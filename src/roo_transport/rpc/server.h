#pragma once

#include <vector>

#include "roo_collections.h"
#include "roo_collections/flat_small_hash_map.h"
#include "roo_threads.h"
#include "roo_threads/mutex.h"
#include "roo_threads/thread.h"
#include "roo_threads/condition_variable.h"
#include "roo_time.h"
#include "roo_transport/messaging/messaging.h"
#include "roo_transport/rpc/internal/server/handler.h"
#include "roo_transport/rpc/serialization.h"
#include "roo_transport/rpc/status.h"

namespace roo_transport {

using FunctionTable =
    roo_collections::FlatSmallHashMap<RpcFunctionId, RpcHandlerFn>;

/// Convenience adapter from a typed synchronous unary handler to the raw RPC
/// server interface.
template <typename Request, typename Response,
          typename RequestDeserializer = Deserializer<Request>,
          typename ResponseSerializer = Serializer<Response>>
class UnaryHandler {
 public:
  using Fn = std::function<RpcStatus(const Request&, Response&)>;

  /// Creates a handler that delegates to `fn`.
  UnaryHandler(Fn fn) : fn_(std::move(fn)) {}

  /// Deserializes the request, runs the handler, and sends the response.
  void operator()(RequestHandle handle, const roo::byte* payload,
                  size_t payload_size, bool fin) const {
    RequestDeserializer deserializer;
    Request req;
    RpcStatus status = deserializer.deserialize(payload, payload_size, req);
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

/// Convenience adapter from a typed asynchronous unary handler to the raw RPC
/// server interface.
template <typename Request, typename Response,
          typename RequestDeserializer = Deserializer<Request>,
          typename ResponseSerializer = Serializer<Response>>
class AsyncUnaryHandler {
 public:
  using Fn = std::function<void(const Request&,
                                std::function<void(RpcStatus, Response)>)>;

  /// Creates a handler that delegates to `fn`.
  AsyncUnaryHandler(Fn fn) : fn_(std::move(fn)) {}

  /// Deserializes the request, runs the handler, and sends the response.
  void operator()(RequestHandle handle, const roo::byte* payload,
                  size_t payload_size, bool fin) const {
    RequestDeserializer deserializer;
    Request req;
    RpcStatus status = deserializer.deserialize(payload, payload_size, req);
    if (status != roo_transport::kOk) {
      handle.sendFailureResponse(status, "request deserialization failed");
      return;
    }
    fn_(req, [handle](RpcStatus resp_status, Response resp_val) {
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

/// Server-side dispatcher for RPC requests received over `Messaging`.
class RpcServer {
 public:
  /// Creates a server that routes requests through `function_table`.
  RpcServer(Messaging& messaging, const FunctionTable* function_table);

  /// Registers the request dispatcher with the messaging transport.
  void begin();
  /// Unregisters the dispatcher, clears pending calls, and joins the timer.
  /// Quiesce incoming dispatch and application handlers before destruction.
  void end();

  /// Destroys the server and unregisters its receiver.
  ~RpcServer() { end(); }

 private:
  friend class RequestHandle;

  class Dispatcher : public Messaging::Receiver {
   public:
    explicit Dispatcher(RpcServer& rpc_server) : rpc_server_(rpc_server) {}

    void received(Messaging::ConnectionId connection_id, const roo::byte* data,
                  size_t len) override {
      rpc_server_.handleRequest(connection_id, data, len);
    }

    void reset(Messaging::ConnectionId connection_id) override {
      rpc_server_.connectionReset(connection_id);
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
                           RpcStreamId stream_id, RpcStatus status,
                           roo::string_view msg);

  // Claims the request once, overriding status if its deadline has expired.
  bool prepForResponse(Messaging::ConnectionId connection_id,
                       RpcStreamId stream_id, RpcStatus& status);

  void reconnected(Messaging::ConnectionId connection_id);

  void connectionReset(Messaging::ConnectionId connection_id);
  void deadlineLoop();
  void sendResponse(Messaging::ConnectionId connection_id, RpcStreamId stream_id,
                    RpcStatus status, const roo::byte* data, size_t len);

  Messaging& messaging_;
  Dispatcher dispatcher_;

  const FunctionTable* handlers_;

  Messaging::ConnectionId connection_id_;

  roo::mutex mutex_;
  roo::condition_variable deadlines_changed_;
  // Started lazily on the first timed request; uses a 4096-byte task stack.
  roo::thread deadline_thread_;
  bool active_ = false;  // Guarded by mutex_.

  // Guarded by mutex_.
  roo_collections::FlatSmallHashMap<RpcStreamId, RpcRequest> pending_calls_;
};

}  // namespace roo_transport