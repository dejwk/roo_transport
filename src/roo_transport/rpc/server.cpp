#include "roo_transport/rpc/server.h"

#include "roo_transport/rpc/internal/header.h"

namespace roo_transport {

RpcServer::RpcServer(Messaging& messaging, const FunctionTable* function_table)
    : messaging_(messaging),
      dispatcher_(*this),
      handlers_(function_table),
      connection_id_(0) {}

void RpcServer::begin() { messaging_.setReceiver(dispatcher_); }

void RpcServer::end() { messaging_.unsetReceiver(); }

void RpcServer::handleRequest(Messaging::ConnectionId connection_id,
                              const roo::byte* data, size_t len) {
  if (connection_id != connection_id_) {
    connection_id_ = connection_id;
    reconnected();
  }
  RpcHeader header;
  size_t header_len = header.deserialize(data, len);
  if (header_len == 0) {
    LOG(WARNING) << "RpcServer: received invalid RPC header";
    return;
  }
  data += header_len;
  len -= header_len;

  if (header.type() != RpcHeader::kRequest) {
    LOG(WARNING) << "RpcServer: received non-request RPC message";
    return;
  }
  if (header.isFirstMessage()) {
    // New request.
    RpcFunctionId function_id = header.functionId();
    auto handler_it = handlers_->find(function_id);
    if (handler_it == handlers_->end()) {
      sendFailureResponse(connection_id, header.streamId(),
                          RpcStatus::kUnimplemented,
                          roo::string_view("Unknown function ID"));
      return;
    }
    const RpcHandlerFn& handler = handler_it->second;

    roo_time::Uptime deadline = roo_time::Uptime::Max();
    if (header.hasTimeout()) {
      deadline = roo_time::Uptime::Now() + roo_time::Millis(header.timeoutMs());
    }

    {
      roo::lock_guard<roo::mutex> guard(mutex_);
      if (pending_calls_.find(header.streamId()) != pending_calls_.end()) {
        LOG(WARNING) << "RpcServer: received duplicate request for stream ID "
                     << header.streamId();
        return;
      }
      pending_calls_.insert(
          {header.streamId(),
           RpcRequest(connection_id, function_id, header.streamId(), deadline,
                      header.isLastMessage())});
    }

    // Invoke the handler.
    handler(RequestHandle(*this, connection_id, header.streamId()), data, len,
            header.isLastMessage());
  } else {
    LOG(FATAL) << "Streaming RPC not yet supported";
  }
}

void RpcServer::reconnected() {
  roo::lock_guard<roo::mutex> guard(mutex_);
  // Clear the info about pending requests, so that new requests don't clash
  // when they use the same stream IDs.
  pending_calls_.clear();
}

void RpcServer::sendSuccessResponse(Messaging::ConnectionId connection_id,
                                    RpcStreamId stream_id,
                                    const roo::byte* data, size_t len) {
  if (!prepForResponse(connection_id, stream_id)) {
    return;
  }
  RpcHeader header = RpcHeader::NewUnaryResponse(stream_id, RpcStatus::kOk);
  roo::byte header_bytes[RpcHeader::kMaxSerializedSize];
  size_t header_size =
      header.serialize(header_bytes, RpcHeader::kMaxSerializedSize);
  messaging_.sendContinuation(connection_id, header_bytes, header_size, data,
                              len);
}

void RpcServer::sendFailureResponse(Messaging::ConnectionId connection_id,
                                    RpcStreamId stream_id, RpcStatus status,
                                    roo::string_view msg) {
  if (!prepForResponse(connection_id, stream_id)) {
    return;
  }
  RpcHeader header = RpcHeader::NewUnaryResponse(stream_id, status);
  roo::byte header_bytes[RpcHeader::kMaxSerializedSize];
  size_t header_size =
      header.serialize(header_bytes, RpcHeader::kMaxSerializedSize);
  messaging_.sendContinuation(connection_id, header_bytes, header_size,
                              (const roo::byte*)msg.data(), msg.size());
}

bool RpcServer::prepForResponse(Messaging::ConnectionId connection_id,
                                RpcStreamId stream_id) {
  // Look up the request.
  roo::lock_guard<roo::mutex> guard(mutex_);
  auto it = pending_calls_.find(stream_id);
  if (it == pending_calls_.end()) {
    LOG(WARNING) << "RpcServer: no pending request for stream ID " << stream_id;
    return false;
  }
  RpcRequest& request = it->second;

  bool ok = false;
  if (request.serverFin()) {
    LOG(WARNING) << "RpcServer: attempt to send response on closed stream ID "
                 << stream_id;
    // } else if (request.isCancelled()) {
    //   LOG(WARNING) << "RpcServer: attempt to send response on cancelled "
    //                   "request for stream ID "
    //                << stream_id;
  } else if (connection_id != connection_id_) {
    LOG(INFO) << "RpcServer: not sending the response because the connection "
                 "has been reset";
  } else {
    ok = true;
  }
  if (request.clientFin() || !ok) {
    pending_calls_.erase(it);
  }
  return ok;
}

}  // namespace roo_transport