#include "noisy_output_stream.h"

#include <memory>

#include "rand.h"

namespace roo_transport {

namespace {
std::unique_ptr<roo::byte[]> PerturbData(const roo::byte* data, size_t size,
                                         int error_rate) {
  std::unique_ptr<roo::byte[]> buf(new roo::byte[size]);
  memcpy(buf.get(), data, size);
  for (size_t i = 0; i < size; ++i) {
    if ((rand() % 10000) < error_rate) {
      buf[i] = static_cast<roo::byte>(rand() & 0xFF);
    }
  }
  return buf;
}
}  // namespace

NoisyOutputStream::NoisyOutputStream(roo_io::OutputStream& out, int error_rate)
    : out_(out), error_rate_(error_rate), counter_(0) {}

void NoisyOutputStream::setErrorRate(int error_rate) {
  error_rate_ = error_rate;
}

size_t NoisyOutputStream::write(const roo::byte* data, size_t len) {
  std::unique_ptr<roo::byte[]> buf = PerturbData(data, len, error_rate_);
  return out_.write(buf.get(), len);
}

size_t NoisyOutputStream::tryWrite(const roo::byte* data, size_t len) {
  std::unique_ptr<roo::byte[]> buf = PerturbData(data, len, error_rate_);
  return out_.tryWrite(buf.get(), len);
}

roo_io::Status NoisyOutputStream::status() const { return out_.status(); }

void NoisyOutputStream::close() { out_.close(); }

}  // namespace roo_transport
