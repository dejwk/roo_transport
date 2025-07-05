#pragma once

#include <functional>

#include "roo_io/core/input_stream.h"

namespace roo_transport {

class SocketInputStream : public roo_io::InputStream {
 public:
  using ReceiveCb = std::function<void()>;

  virtual void onReceive(ReceiveCb recv_cb) = 0;

  virtual size_t available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
};

}