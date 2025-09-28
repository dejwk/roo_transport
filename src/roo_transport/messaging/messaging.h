#pragma once

#include <stdint.h>

#include <functional>

namespace roo_transport {

class Messaging {
 public:
  ~Messaging() = default;
  virtual void send(const void* data, size_t size) = 0;
  virtual void registerRecvCallback(
      std::function<void(const void* data, size_t len)> cb) = 0;
};

}  // namespace roo_transport
