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

class RpcRequest {
 public:
  RpcRequest() = default;
  RpcRequest(const RpcRequest&) = default;
  RpcRequest& operator=(const RpcRequest&) = default;

  RpcRequest(RpcServer& server, Messaging::ConnectionId connection_id,
             RpcFunctionId function_id, RpcStreamId stream_id,
             roo_time::Uptime deadline, bool fin)
      : server_(&server),
        connection_id_(connection_id),
        function_id_(function_id),
        stream_id_(stream_id),
        deadline_(deadline),
        client_closed_(fin),
        server_closed_(false) {}

  Messaging::ConnectionId connectionId() const { return connection_id_; }

  RpcFunctionId functionId() const { return function_id_; }
  RpcStreamId streamId() const { return stream_id_; }

  roo_time::Uptime deadline() const { return deadline_; }

  void sendSuccessResponse(const roo::byte* payload, size_t payload_size,
                           bool last);

  void sendFailureResponse(Status status, roo::string_view msg);

  bool clientFin() const { return client_closed_; }
  bool serverFin() const { return server_closed_; }

 private:
  RpcServer* server_;
  Messaging::ConnectionId connection_id_;
  RpcFunctionId function_id_;
  RpcStreamId stream_id_;
  roo_time::Uptime deadline_;
  bool client_closed_;
  bool server_closed_;
};

}  // namespace roo_transport
