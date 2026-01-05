#include "roo_transport/rpc/internal/header.h"

#include "roo_io/data/read.h"
#include "roo_io/data/write.h"
#include "roo_io/memory/memory_input_iterator.h"
#include "roo_io/memory/memory_output_iterator.h"

namespace roo_transport {

RpcFunctionId RpcHeader::functionId() const {
  CHECK(type_ == kRequest && first_message_);
  return new_request_.function_id_;
}

uint32_t RpcHeader::timeoutMs() const {
  CHECK(type_ == kRequest && first_message_ && has_timeout_);
  return new_request_.timeout_ms_;
}

RpcStatus RpcHeader::responseStatus() const {
  CHECK(type_ == kResponse && last_message_);
  return last_response_.status_;
}

RpcHeader RpcHeader::NewUnaryRequest(RpcFunctionId function_id,
                                     RpcStreamId stream_id) {
  RpcHeader header;
  header.type_ = kRequest;
  header.first_message_ = true;
  header.last_message_ = true;
  header.has_timeout_ = false;
  header.stream_id_ = stream_id;
  header.new_request_.function_id_ = function_id;
  header.new_request_.timeout_ms_ = 0;
  return header;
}

RpcHeader RpcHeader::NewUnaryRequest(RpcFunctionId function_id,
                                     RpcStreamId stream_id,
                                     uint32_t timeout_ms) {
  RpcHeader header;
  header.type_ = kRequest;
  header.first_message_ = true;
  header.last_message_ = true;
  header.has_timeout_ = false;
  header.stream_id_ = stream_id;
  header.new_request_.function_id_ = function_id;
  header.new_request_.timeout_ms_ = timeout_ms;
  return header;
}

RpcHeader RpcHeader::NewUnaryResponse(RpcStreamId stream_id, RpcStatus status) {
  RpcHeader header;
  header.type_ = kResponse;
  header.first_message_ = true;
  header.last_message_ = true;
  header.has_timeout_ = false;
  header.stream_id_ = stream_id;
  header.last_response_.status_ = status;
  return header;
}

size_t RpcHeader::serialize(roo::byte* buffer, size_t buffer_size) const {
  roo_io::MemoryOutputIterator it(buffer, buffer + buffer_size);
  uint8_t mask = 0;
  mask |= (type_ == kRequest) ? 0x01 : 0;
  mask |= first_message_ ? 0x02 : 0;
  mask |= last_message_ ? 0x04 : 0;
  mask |= has_timeout_ ? 0x08 : 0;
  it.write(roo::byte{mask});
  roo_io::WriteBeU24(it, stream_id_);
  if (type_ == kRequest && first_message_) {
    roo_io::WriteVarU64(it, new_request_.function_id_);
    if (has_timeout_) {
      roo_io::WriteVarU64(it, new_request_.timeout_ms_);
    }
  } else if (type_ == kResponse && last_message_) {
    roo_io::WriteU8(it, (uint8_t)last_response_.status_);
  }
  CHECK_EQ(roo_io::kOk, it.status());
  return it.ptr() - buffer;
}

size_t RpcHeader::deserialize(const roo::byte* buffer, size_t buffer_size) {
  roo_io::MemoryIterator it(buffer, buffer + buffer_size);
  uint8_t mask = (uint8_t)roo_io::ReadU8(it);
  if ((mask & 0xF0) != 0) {
    return 0;
  }
  type_ = (mask & 0x01) ? kRequest : kResponse;
  first_message_ = (mask & 0x02) != 0;
  last_message_ = (mask & 0x04) != 0;
  has_timeout_ = (mask & 0x08) != 0;
  stream_id_ = roo_io::ReadBeU24(it);
  if (type_ == kRequest && first_message_) {
    new_request_.function_id_ = roo_io::ReadVarU64(it);
    if (has_timeout_) {
      new_request_.timeout_ms_ = roo_io::ReadVarU64(it);
    } else {
      new_request_.timeout_ms_ = 0;
    }
  } else if (type_ == kResponse && last_message_) {
    last_response_.status_ = (RpcStatus)roo_io::ReadU8(it);
  }
  if (it.status() != roo_io::kOk) {
    return 0;
  }
  return it.ptr() - buffer;
}

}  // namespace roo_transport
