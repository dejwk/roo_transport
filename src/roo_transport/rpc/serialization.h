#pragma once

#include <memory>

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_io/memory/load.h"
#include "roo_io/memory/store.h"
#include "roo_transport/rpc/status.h"

namespace roo_transport {

template <typename T>
struct Serializer;

template <typename T>
struct Deserializer;

class SimpleSerialized {
 public:
  SimpleSerialized() : status_(kOk), data_(nullptr), size_(0) {}

  SimpleSerialized(roo_transport::Status error)
      : status_(error), data_(nullptr), size_(0) {}

  SimpleSerialized(std::unique_ptr<roo::byte[]> data, size_t size)
      : status_(kOk), data_(std::move(data)), size_(size) {}

  roo_transport::Status status() const { return status_; }
  const roo::byte* data() const { return data_.get(); }
  size_t size() const { return size_; }

 private:
  roo_transport::Status status_;
  std::unique_ptr<roo::byte[]> data_;
  size_t size_;
};

template <>
struct Serializer<uint32_t> {
  SimpleSerialized operator()(uint32_t val) const {
    std::unique_ptr<roo::byte[]> data(new roo::byte[4]);
    roo_io::StoreBeU32(val, data.get());
    return SimpleSerialized(std::move(data), 4);
  }
};

template <>
struct Deserializer<uint32_t> {
  Status operator()(const roo::byte* data, size_t len, uint32_t& result) const {
    if (len != 4) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadBeU32(data);
    return roo_transport::kOk;
  }
};

}  // namespace roo_transport
