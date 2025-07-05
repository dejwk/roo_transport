#pragma once

#include "roo_io/core/output_stream.h"

namespace roo_transport {

class SocketOutputStream : public roo_io::OutputStream {
 public:
  virtual size_t availableForWrite() = 0;
};

}
