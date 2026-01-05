#include "roo_transport/rpc/internal/server/request.h"

#include "roo_transport/rpc/server.h"

namespace roo_transport {

void RequestHandle::sendSuccessResponse(const roo::byte* payload,
                                        size_t payload_size, bool last) const {
  server_->sendSuccessResponse(connection_id_, stream_id_, payload,
                               payload_size);
}

void RequestHandle::sendFailureResponse(RpcStatus status,
                                        roo::string_view msg) const {
  server_->sendFailureResponse(connection_id_, stream_id_, status, msg);
}

}  // namespace roo_transport
