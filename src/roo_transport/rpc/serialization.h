#pragma once

#include <memory>

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_backport/string_view.h"
#include "roo_io/memory/load.h"
#include "roo_io/memory/store.h"
#include "roo_transport/rpc/status.h"

namespace roo_transport {

template <typename T>
struct Serializer;

template <typename T>
struct Deserializer;

// For fixed-size small payloads that can be just copied by value.
template <size_t N>
class StaticSerialized {
 public:
  StaticSerialized() = default;

  roo_transport::Status status() const { return kOk; }
  const roo::byte* data() const { return data_; }
  size_t size() const { return N; }

  roo::byte* data() { return data_; }

 private:
  roo::byte data_[N];
};

// For dynamically sized payloads. Stores the data array on the heap.
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

// Simple serializers and deserializers for basic types are provided below.

// Boolean.

template <>
struct Serializer<bool> {
  StaticSerialized<1> operator()(bool val) const {
    StaticSerialized<1> result;
    roo_io::StoreU8(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<bool> {
  Status operator()(const roo::byte* data, size_t len, bool& result) const {
    if (len != 1) {
      return roo_transport::kInvalidArgument;
    }
    uint8_t val = roo_io::LoadU8(data);
    if (val > 1) {
      return roo_transport::kInvalidArgument;
    }
    result = (val != 0);
    return roo_transport::kOk;
  }
};

// Int8.

template <>
struct Serializer<int8_t> {
  StaticSerialized<1> operator()(int8_t val) const {
    StaticSerialized<1> result;
    roo_io::StoreS8(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<int8_t> {
  Status operator()(const roo::byte* data, size_t len, int8_t& result) const {
    if (len != 1) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadS8(data);
    return roo_transport::kOk;
  }
};

// UInt8.

template <>
struct Serializer<uint8_t> {
  StaticSerialized<1> operator()(uint8_t val) const {
    StaticSerialized<1> result;
    roo_io::StoreU8(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<uint8_t> {
  Status operator()(const roo::byte* data, size_t len, uint8_t& result) const {
    if (len != 1) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadU8(data);
    return roo_transport::kOk;
  }
};

// Int16.

template <>
struct Serializer<int16_t> {
  StaticSerialized<2> operator()(int16_t val) const {
    StaticSerialized<2> result;
    roo_io::StoreBeS16(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<int16_t> {
  Status operator()(const roo::byte* data, size_t len, int16_t& result) const {
    if (len != 2) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadBeS16(data);
    return roo_transport::kOk;
  }
};

// UInt16.

template <>
struct Serializer<uint16_t> {
  StaticSerialized<2> operator()(uint16_t val) const {
    StaticSerialized<2> result;
    roo_io::StoreBeU16(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<uint16_t> {
  Status operator()(const roo::byte* data, size_t len, uint16_t& result) const {
    if (len != 2) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadBeU16(data);
    return roo_transport::kOk;
  }
};

// Int32.

template <>
struct Serializer<int32_t> {
  StaticSerialized<4> operator()(int32_t val) const {
    StaticSerialized<4> result;
    roo_io::StoreBeS32(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<int32_t> {
  Status operator()(const roo::byte* data, size_t len, int32_t& result) const {
    if (len != 4) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadBeS32(data);
    return roo_transport::kOk;
  }
};

// UInt32.

template <>
struct Serializer<uint32_t> {
  StaticSerialized<4> operator()(uint32_t val) const {
    StaticSerialized<4> result;
    roo_io::StoreBeU32(val, result.data());
    return result;
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

// Int64.

template <>
struct Serializer<int64_t> {
  StaticSerialized<8> operator()(int64_t val) const {
    StaticSerialized<8> result;
    roo_io::StoreBeS64(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<int64_t> {
  Status operator()(const roo::byte* data, size_t len, int64_t& result) const {
    if (len != 8) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadBeS64(data);
    return roo_transport::kOk;
  }
};

// UInt64.

template <>
struct Serializer<uint64_t> {
  StaticSerialized<8> operator()(uint64_t val) const {
    StaticSerialized<8> result;
    roo_io::StoreBeU64(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<uint64_t> {
  Status operator()(const roo::byte* data, size_t len, uint64_t& result) const {
    if (len != 8) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadBeU64(data);
    return roo_transport::kOk;
  }
};

// String.

class SerializedByteArrayAdapter {
 public:
  SerializedByteArrayAdapter(const roo::byte* data, size_t size)
      : data_(data), size_(size) {}

  Status status() const { return kOk; }
  const roo::byte* data() const { return data_; }
  size_t size() const { return size_; }

 private:
  const roo::byte* data_;
  size_t size_;
};

template <>
struct Serializer<roo::string_view> {
  SerializedByteArrayAdapter operator()(roo::string_view val) const {
    return SerializedByteArrayAdapter(
        reinterpret_cast<const roo::byte*>(val.data()), val.size());
  }
};

template <>
struct Deserializer<roo::string_view> {
  Status operator()(const roo::byte* data, size_t len,
                    roo::string_view& result) const {
    result = roo::string_view(reinterpret_cast<const char*>(data), len);
    return roo_transport::kOk;
  }
};

}  // namespace roo_transport
