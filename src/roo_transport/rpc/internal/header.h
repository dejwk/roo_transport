#pragma once

#include <stdint.h>

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_transport/rpc/rpc.h"
#include "roo_transport/rpc/status.h"

namespace roo_transport {

class RpcHeader {
 public:
  enum MessageType { kUnset = -1, kRequest = 0, kResponse = 1 };

  static constexpr size_t kMaxSerializedSize = 32;

  RpcHeader()
      : type_(kUnset),
        first_message_(false),
        last_message_(false),
        has_timeout_(false),
        stream_id_(0) {}

  static RpcHeader NewUnaryRequest(RpcFunctionId function_id,
                                   RpcStreamId stream_id);

  static RpcHeader NewUnaryRequest(RpcFunctionId function_id,
                                   RpcStreamId stream_id, uint32_t timeout_ms);

  static RpcHeader NewUnaryResponse(RpcStreamId stream_id, Status status);

  size_t serialize(roo::byte* buffer, size_t buffer_size) const;
  size_t deserialize(const roo::byte* buffer, size_t buffer_size);

  MessageType type() const { return type_; }
  bool isFirstMessage() const { return first_message_; }
  bool isLastMessage() const { return last_message_; }
  bool hasTimeout() const { return has_timeout_; }
  RpcStreamId streamId() const { return stream_id_; }

  RpcFunctionId functionId() const;
  uint32_t timeoutMs() const;

  Status responseStatus() const;

 private:
  MessageType type_;
  bool first_message_;
  bool last_message_;
  bool has_timeout_;
  RpcStreamId stream_id_;

  union {
    struct {
      RpcFunctionId function_id_;
      uint32_t timeout_ms_;
    } new_request_;
    struct {
      Status status_;
    } last_response_;
    struct {
      // No additional fields.
    } continuation_;
  };
};

}  // namespace roo_transport
