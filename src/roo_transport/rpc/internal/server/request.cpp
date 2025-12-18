#include "roo_transport/rpc/internal/server/request.h"

#include "roo_transport/rpc/server.h"

namespace roo_transport {

void RpcRequest::sendSuccessResponse(const roo::byte* payload,
                                     size_t payload_size, bool last) {
  server_->sendSuccessResponse(connection_id_, stream_id_, payload,
                               payload_size);
}

void RpcRequest::sendFailureResponse(Status status, roo::string_view msg) {
  server_->sendFailureResponse(connection_id_, stream_id_, status, msg);
}

}  // namespace roo_transport
