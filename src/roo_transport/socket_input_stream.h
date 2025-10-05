#pragma once

#include <functional>

#include "roo_io/core/input_stream.h"

namespace roo_transport {

class SocketInputStream : public roo_io::InputStream {
 public:
  virtual size_t available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
};

}