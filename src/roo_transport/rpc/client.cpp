#include "roo_transport/rpc/client.h"

#include "roo_transport/rpc/internal/header.h"

namespace roo_transport {

RpcClient::RpcClient(Messaging& messaging)
    : messaging_(messaging), dispatcher_(*this), next_stream_id_(1) {}

void RpcClient::begin() { messaging_.setReceiver(dispatcher_); }

void RpcClient::end() { messaging_.unsetReceiver(); }

RpcStatus RpcClient::sendUnaryRpc(RpcFunctionId function_id,
                                  const roo::byte* payload, size_t payload_size,
                                  RpcClient::UnaryCompletionCb cb) {
  uint32_t stream_id = new_stream(std::move(cb));
  RpcHeader header = RpcHeader::NewUnaryRequest(function_id, stream_id);
  roo::byte header_bytes[RpcHeader::kMaxSerializedSize];
  size_t header_size =
      header.serialize(header_bytes, RpcHeader::kMaxSerializedSize);
  CHECK(header_size > 0);
  Messaging::ConnectionId connection_id;
  return messaging_.send(header_bytes, header_size, payload, payload_size,
                         &connection_id)
             ? RpcStatus::kOk
             : RpcStatus::kUnavailable;
}

RpcStatus RpcClient::sendUnaryRpcWithTimeout(RpcFunctionId function_id,
                                             const roo::byte* payload,
                                             size_t payload_size,
                                             uint32_t timeout_ms,
                                             RpcClient::UnaryCompletionCb cb) {
  uint32_t stream_id = new_stream(std::move(cb));
  RpcHeader header =
      RpcHeader::NewUnaryRequest(function_id, stream_id, timeout_ms);
  roo::byte header_bytes[RpcHeader::kMaxSerializedSize];
  size_t header_size =
      header.serialize(header_bytes, RpcHeader::kMaxSerializedSize);
  CHECK(header_size > 0);
  Messaging::ConnectionId connection_id;
  return messaging_.send(header_bytes, header_size, payload, payload_size,
                         &connection_id)
             ? RpcStatus::kOk
             : RpcStatus::kUnavailable;
}

RpcStreamId RpcClient::new_stream(RpcClient::UnaryCompletionCb cb) {
  roo::lock_guard<roo::mutex> guard(mutex_);
  RpcStreamId stream_id = next_stream_id_++;
  if (next_stream_id_ > 0x00FFFFFF) {
    next_stream_id_ = 1;
  }
  outgoing_calls_.insert({stream_id, std::move(cb)});
  return stream_id;
}

void RpcClient::handleResponse(Messaging::ConnectionId connection_id,
                               const roo::byte* data, size_t len) {
  RpcHeader header;
  size_t header_len = header.deserialize(data, len);
  if (header_len == 0) {
    LOG(WARNING) << "RpcClient: received invalid RPC header";
    return;
  }
  data += header_len;
  len -= header_len;

  if (header.type() != RpcHeader::kResponse) {
    LOG(WARNING) << "RpcClient: received non-response RPC message";
    return;
  }

  UnaryCompletionCb cb;
  {
    roo::lock_guard<roo::mutex> guard(mutex_);
    auto it = outgoing_calls_.find(header.streamId());
    if (it == outgoing_calls_.end()) {
      LOG(WARNING) << "RpcClient: received response for unknown stream ID "
                   << header.streamId();
      return;
    }
    cb = std::move(it->second);
    if (header.isLastMessage()) {
      outgoing_calls_.erase(it);
    }
  }

  RpcStatus status = RpcStatus::kOk;
  if (header.isLastMessage()) {
    status = header.responseStatus();
  }
  // Call the response callback.
  cb(data, len, status);
}

void RpcClient::connectionReset(Messaging::ConnectionId connection_id) {
  OutgoingCalls calls_to_cancel;
  {
    roo::lock_guard<roo::mutex> guard(mutex_);
    calls_to_cancel = std::move(outgoing_calls_);
    outgoing_calls_ = OutgoingCalls();
  }
  for (auto it = calls_to_cancel.begin(); it != calls_to_cancel.end(); ++it) {
    // Call the response callback with cancelled status.
    it->second(nullptr, 0, kUnavailable);
  }
}

}  // namespace roo_transport