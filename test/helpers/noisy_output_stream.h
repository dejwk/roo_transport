#pragma once

#include "roo_io/core/output_stream.h"

namespace roo_transport {

class NoisyOutputStream : public roo_io::OutputStream {
 public:
  NoisyOutputStream(roo_io::OutputStream& out, int error_rate);

  void setErrorRate(int error_rate);

  size_t write(const roo::byte* data, size_t len) override;

  size_t tryWrite(const roo::byte* data, size_t len) override;

  roo_io::Status status() const override;

  void close() override;

 private:
  roo_io::OutputStream& out_;
  int error_rate_;
  int counter_;
};

}  // namespace roo_transport