#pragma once

#include <functional>

#include "roo_transport/rpc/internal/server/request.h"

namespace roo_transport {

using RpcHandlerFn =
    std::function<void(RequestHandle handle, const roo::byte* payload,
                       size_t payload_size, bool fin)>;

}  // namespace roo_transport
