#pragma once

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_backport/string_view.h"
#include "roo_time.h"
#include "roo_transport/messaging/messaging.h"
#include "roo_transport/rpc/rpc.h"
#include "roo_transport/rpc/status.h"

namespace roo_transport {

class RpcServer;

/// Server-maintained state for one in-flight RPC request.
class RpcRequest {
 public:
  /// Creates an empty request record.
  RpcRequest() = default;
  /// Copies request state.
  RpcRequest(const RpcRequest&) = default;
  /// Assigns request state.
  RpcRequest& operator=(const RpcRequest&) = default;

  /// Creates request state for a new incoming RPC stream.
  RpcRequest(Messaging::ConnectionId connection_id, RpcFunctionId function_id,
             RpcStreamId stream_id, roo_time::Uptime deadline, bool fin)
      : connection_id_(connection_id),
        function_id_(function_id),
        stream_id_(stream_id),
        deadline_(deadline),
        client_closed_(fin),
        server_closed_(false) {}

  /// Returns the transport connection that owns the request.
  Messaging::ConnectionId connectionId() const { return connection_id_; }

  /// Returns the target RPC function id.
  RpcFunctionId functionId() const { return function_id_; }
  /// Returns the RPC stream id.
  RpcStreamId streamId() const { return stream_id_; }

  /// Returns the request deadline.
  roo_time::Uptime deadline() const { return deadline_; }

  /// Returns whether the client already closed its request stream.
  bool clientFin() const { return client_closed_; }
  /// Returns whether the server already closed its response stream.
  bool serverFin() const { return server_closed_; }

 private:
  Messaging::ConnectionId connection_id_;
  RpcFunctionId function_id_;
  RpcStreamId stream_id_;
  roo_time::Uptime deadline_;
  bool client_closed_;
  bool server_closed_;
};

/// Lightweight handle used by handlers to send responses for one request.
class RequestHandle {
 public:
  /// Creates a handle for `stream_id` on `connection_id`.
  RequestHandle(RpcServer& server, Messaging::ConnectionId connection_id,
                RpcStreamId stream_id)
      : server_(&server),
        connection_id_(connection_id),
        stream_id_(stream_id) {}

  /// Sends a successful response payload.
  void sendSuccessResponse(const roo::byte* payload, size_t payload_size,
                           bool last) const;

  /// Sends a failure response with the provided status and message.
  void sendFailureResponse(RpcStatus status, roo::string_view msg) const;

 private:
  mutable RpcServer* server_;
  Messaging::ConnectionId connection_id_;
  RpcStreamId stream_id_;
};

}  // namespace roo_transport
