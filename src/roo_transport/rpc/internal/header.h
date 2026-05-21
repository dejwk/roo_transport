#pragma once

#include <stdint.h>

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_transport/rpc/rpc.h"
#include "roo_transport/rpc/status.h"

namespace roo_transport {

/// Wire-format header describing one RPC request or response message.
class RpcHeader {
 public:
  /// Kind of RPC message represented by the header.
  enum MessageType { kUnset = -1, kRequest = 0, kResponse = 1 };

  /// Maximum serialized size of any RPC header.
  static constexpr size_t kMaxSerializedSize = 32;

  /// Creates an unset header.
  RpcHeader()
      : type_(kUnset),
        first_message_(false),
        last_message_(false),
        has_timeout_(false),
        stream_id_(0) {}

  /// Creates a header for a unary request without timeout metadata.
  static RpcHeader NewUnaryRequest(RpcFunctionId function_id,
                                   RpcStreamId stream_id);

  /// Creates a header for a unary request with timeout metadata.
  static RpcHeader NewUnaryRequest(RpcFunctionId function_id,
                                   RpcStreamId stream_id, uint32_t timeout_ms);

  /// Creates a header for a unary response.
  static RpcHeader NewUnaryResponse(RpcStreamId stream_id, RpcStatus status);

  /// Serializes the header into `buffer`.
  ///
  /// @return Number of bytes written.
  size_t serialize(roo::byte* buffer, size_t buffer_size) const;
  /// Parses header fields from `buffer`.
  ///
  /// @return Number of bytes consumed.
  size_t deserialize(const roo::byte* buffer, size_t buffer_size);

  /// Returns the message type encoded by the header.
  MessageType type() const { return type_; }
  /// Returns whether this is the first message in the stream.
  bool isFirstMessage() const { return first_message_; }
  /// Returns whether this is the last message in the stream.
  bool isLastMessage() const { return last_message_; }
  /// Returns whether the header carries timeout metadata.
  bool hasTimeout() const { return has_timeout_; }
  /// Returns the RPC stream id.
  RpcStreamId streamId() const { return stream_id_; }

  /// Returns the target function id for request headers.
  RpcFunctionId functionId() const;
  /// Returns the timeout in milliseconds for timed requests.
  uint32_t timeoutMs() const;

  /// Returns the response status for response headers.
  RpcStatus responseStatus() const;

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
      RpcStatus status_;
    } last_response_;
    struct {
      // No additional fields.
    } continuation_;
  };
};

}  // namespace roo_transport
