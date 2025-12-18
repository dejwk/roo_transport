#pragma once;

#include <vector>

#include "roo_collections.h"
#include "roo_collections/flat_small_hash_map.h"
#include "roo_threads.h"
#include "roo_threads/mutex.h"
#include "roo_time.h"
#include "roo_transport/messaging/messaging.h"
#include "roo_transport/rpc/internal/server/handler.h"
#include "roo_transport/rpc/status.h"

namespace roo_transport {

using FunctionTable =
    roo_collections::FlatSmallHashMap<RpcFunctionId, RpcHandlerFn>;

class RpcServer {
 public:
  RpcServer(Messaging& messaging, const FunctionTable* function_table);

  void begin();
  void end();

  ~RpcServer() { messaging_.unsetReceiver(); }

 private:
  friend class RpcRequest;

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
  roo_collections::FlatSmallHashMap<RpcFunctionId, RpcRequest> pending_calls_;
};

}  // namespace roo_transport