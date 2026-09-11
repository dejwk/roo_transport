#include "roo_transport/rpc/server.h"

#include "roo_transport/rpc/internal/header.h"

namespace roo_transport {

RpcServer::RpcServer(Messaging& messaging, const FunctionTable* function_table)
    : messaging_(messaging),
      dispatcher_(*this),
      handlers_(function_table),
      connection_id_(0) {}

void RpcServer::begin() {
  roo::lock_guard<roo::mutex> guard(mutex_);
  active_ = true;
  messaging_.setReceiver(dispatcher_);
}

void RpcServer::end() {
  messaging_.unsetReceiver();
  {
    roo::lock_guard<roo::mutex> guard(mutex_);
    active_ = false;
    pending_calls_.clear();
    deadlines_changed_.notify_all();
  }
  if (deadline_thread_.joinable()) deadline_thread_.join();
}

void RpcServer::handleRequest(Messaging::ConnectionId connection_id,
                              const roo::byte* data, size_t len) {
  reconnected(connection_id);
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
      if (!active_) return;
      pending_calls_.insert(
          {header.streamId(),
           RpcRequest(connection_id, function_id, header.streamId(), deadline,
                      header.isLastMessage())});
      if (header.hasTimeout() && !deadline_thread_.joinable()) {
        roo::thread::attributes attrs;
        attrs.set_name("rpcDeadline");
        attrs.set_stack_size(4096);
        deadline_thread_ = roo::thread(attrs, [this] { deadlineLoop(); });
      }
      deadlines_changed_.notify_all();
    }

    if (deadline <= roo_time::Uptime::Now()) {
      sendFailureResponse(connection_id, header.streamId(), kDeadlineExceeded,
                          "deadline exceeded");
      return;
    }

    auto handler_it = handlers_->find(function_id);
    if (handler_it == handlers_->end()) {
      sendFailureResponse(connection_id, header.streamId(),
                          RpcStatus::kUnimplemented,
                          roo::string_view("Unknown function ID"));
      return;
    }
    const RpcHandlerFn& handler = handler_it->second;

    // Invoke the handler.
    handler(RequestHandle(*this, connection_id, header.streamId()), data, len,
            header.isLastMessage());
  } else {
    LOG(FATAL) << "Streaming RPC not yet supported";
  }
}

void RpcServer::reconnected(Messaging::ConnectionId connection_id) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  if (connection_id == connection_id_) return;
  connection_id_ = connection_id;
  // Clear the info about pending requests, so that new requests don't clash
  // when they use the same stream IDs.
  pending_calls_.clear();
  deadlines_changed_.notify_all();
}

void RpcServer::connectionReset(Messaging::ConnectionId connection_id) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  if (connection_id != connection_id_) return;
  pending_calls_.clear();
  connection_id_ = 0;
  deadlines_changed_.notify_all();
}

void RpcServer::sendSuccessResponse(Messaging::ConnectionId connection_id,
                                    RpcStreamId stream_id,
                                    const roo::byte* data, size_t len) {
  RpcStatus status = kOk;
  if (!prepForResponse(connection_id, stream_id, status)) return;
  if (status == kDeadlineExceeded) {
    data = nullptr;
    len = 0;
  }
  sendResponse(connection_id, stream_id, status, data, len);
}

void RpcServer::sendFailureResponse(Messaging::ConnectionId connection_id,
                                    RpcStreamId stream_id, RpcStatus status,
                                    roo::string_view msg) {
  if (!prepForResponse(connection_id, stream_id, status)) return;
  if (status == kDeadlineExceeded) msg = "deadline exceeded";
  sendResponse(connection_id, stream_id, status,
               reinterpret_cast<const roo::byte*>(msg.data()), msg.size());
}

void RpcServer::sendResponse(Messaging::ConnectionId connection_id,
                             RpcStreamId stream_id, RpcStatus status,
                             const roo::byte* data, size_t len) {
  RpcHeader header = RpcHeader::NewUnaryResponse(stream_id, status);
  roo::byte bytes[RpcHeader::kMaxSerializedSize];
  size_t size = header.serialize(bytes, sizeof(bytes));
  messaging_.sendContinuation(connection_id, bytes, size, data, len);
}

bool RpcServer::prepForResponse(Messaging::ConnectionId connection_id,
                                RpcStreamId stream_id, RpcStatus& status) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  // A stale handler must not consume a new connection's reused stream ID.
  if (connection_id != connection_id_) return false;
  auto it = pending_calls_.find(stream_id);
  if (it == pending_calls_.end()) return false;
  const RpcRequest& request = it->second;
  if (request.connectionId() != connection_id) return false;
  if (request.deadline() <= roo_time::Uptime::Now()) {
    status = kDeadlineExceeded;
  }
  // Unary requests have exactly one final response, including expiry.
  pending_calls_.erase(it);
  deadlines_changed_.notify_all();
  return true;
}

void RpcServer::deadlineLoop() {
  roo::unique_lock<roo::mutex> lock(mutex_);
  while (active_) {
    auto next = pending_calls_.end();
    roo_time::Uptime deadline = roo_time::Uptime::Max();
    for (auto it = pending_calls_.begin(); it != pending_calls_.end(); ++it) {
      if (it->second.deadline() < deadline) {
        deadline = it->second.deadline();
        next = it;
      }
    }
    if (next == pending_calls_.end()) {
      deadlines_changed_.wait(lock);
    } else if (deadline > roo_time::Uptime::Now()) {
      deadlines_changed_.wait_until(lock, deadline);
    } else {
      RpcRequest request = next->second;
      pending_calls_.erase(next);
      // Sending may block or dispatch application code. Do not hold mutex_.
      lock.unlock();
      sendResponse(request.connectionId(), request.streamId(),
                   kDeadlineExceeded, nullptr, 0);
      lock.lock();
    }
  }
}

}  // namespace roo_transport
