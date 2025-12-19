#pragma once

#include <memory>

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_backport/string_view.h"
#include "roo_io/data/read.h"
#include "roo_io/data/write.h"
#include "roo_io/memory/load.h"
#include "roo_io/memory/store.h"
#include "roo_transport/rpc/status.h"

namespace roo_transport {

struct Void {};

template <typename T>
struct Serializer;

template <typename T>
struct Deserializer;

struct NullSerialized {
  roo_transport::Status status() const { return kOk; }
  const roo::byte* data() const { return nullptr; }
  size_t size() const { return 0; }
};

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

// For variably-sized payloads. Stores the data array on the heap.
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

// For dynamically-sized payloads. Supports append and write in the middle.
class DynamicSerialized {
 public:
  DynamicSerialized() : status_(kOk), data_(), pos_(0) {}

  //   DynamicSerialized(roo_transport::Status error)
  //       : status_(error), data_() {}

  //   DynamicSerialized(std::unique_ptr<roo::byte[]> data, size_t size)
  //       : status_(kOk), data_(std::move(data)), size_(size) {}

  void fail(Status status) {
    status_ = status;
    data_.clear();
    pos_ = 0;
  }

  roo_transport::Status status() const { return status_; }

  const roo::byte* data() const {
    return data_.empty() ? nullptr : &*data_.begin();
  }

  size_t pos() const { return pos_; }
  size_t size() const { return data_.size(); }

  bool eos() const { return pos_ >= data_.size(); }

  // Implementation of the iterator contract.
  void write(roo::byte b) {
    if (eos()) {
      if (pos_ > data_.size()) {
        data_.insert(data_.end(), pos_ - data_.size(), roo::byte{0});
      }
      data_.push_back(b);
    } else {
      data_[pos_] = b;
    }
    pos_++;
  }

  size_t write(const roo::byte* buf, size_t count) {
    if (pos_ > data_.size()) {
      data_.insert(data_.end(), pos_ - data_.size(), roo::byte{0});
    }
    size_t copy_over = data_.size() - pos_;
    if (copy_over > count) {
      copy_over = count;
    }
    std::copy(buf, buf + copy_over, data_.begin() + pos_);
    pos_ += copy_over;
    buf += copy_over;
    size_t remaining = count - copy_over;
    if (remaining == 0) {
      return copy_over;
    }
    data_.insert(data_.end(), buf, buf + remaining);
    pos_ += remaining;
    return count;
  }

  void seek(size_t position) { pos_ = position; }

 private:
  roo_transport::Status status_;
  std::vector<roo::byte> data_;
  size_t pos_;
};

// Default implementation that implements SerializeInto in terms of serialize().
template <typename T, typename Itr, typename S = Serializer<T>, typename = void>
struct IntoSerializer {
  void operator()(const T& val, Itr& output) const {
    Serializer<T> serializer;
    auto serialized = serializer.serialize(val);
    output.write(serialized.data(), serialized.size());
  }
};

// Override for serializers that provide serializeInto().
template <typename T, typename Itr>
struct IntoSerializer<T, Itr, Serializer<T>,
                      decltype(std::declval<Serializer<T>>().serializeInto(
                                   std::declval<const T&>(),
                                   std::declval<Itr&>()),
                               void())> {
  void operator()(const T& val, Itr& output) const {
    Serializer<T> serializer;
    serializer.serializeInto(val, output);
  }
};

template <typename T, typename Itr>
void SerializeInto(const T& val, Itr& output) {
  IntoSerializer<T, Itr>()(val, output);
}

// Simple serializers and deserializers for basic types are provided below.

// Void.

template <>
struct Serializer<Void> {
  NullSerialized serialize(const Void&) const { return NullSerialized(); }
};

template <>
struct Deserializer<Void> {
  roo_transport::Status deserialize(const roo::byte* data, size_t len,
                                    Void& result) const {
    if (len != 0) {
      return roo_transport::kInvalidArgument;
    }
    return roo_transport::kOk;
  }
};

// Boolean.

template <>
struct Serializer<bool> {
  StaticSerialized<1> serialize(bool val) const {
    StaticSerialized<1> result;
    roo_io::StoreU8(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<bool> {
  Status deserialize(const roo::byte* data, size_t len, bool& result) const {
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
  StaticSerialized<1> serialize(int8_t val) const {
    StaticSerialized<1> result;
    roo_io::StoreS8(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<int8_t> {
  Status deserialize(const roo::byte* data, size_t len, int8_t& result) const {
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
  StaticSerialized<1> serialize(uint8_t val) const {
    StaticSerialized<1> result;
    roo_io::StoreU8(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<uint8_t> {
  Status deserialize(const roo::byte* data, size_t len, uint8_t& result) const {
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
  StaticSerialized<2> serialize(int16_t val) const {
    StaticSerialized<2> result;
    roo_io::StoreBeS16(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<int16_t> {
  Status deserialize(const roo::byte* data, size_t len, int16_t& result) const {
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
  StaticSerialized<2> serialize(uint16_t val) const {
    StaticSerialized<2> result;
    roo_io::StoreBeU16(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<uint16_t> {
  Status deserialize(const roo::byte* data, size_t len,
                     uint16_t& result) const {
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
  StaticSerialized<4> serialize(int32_t val) const {
    StaticSerialized<4> result;
    roo_io::StoreBeS32(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<int32_t> {
  Status deserialize(const roo::byte* data, size_t len, int32_t& result) const {
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
  StaticSerialized<4> serialize(uint32_t val) const {
    StaticSerialized<4> result;
    roo_io::StoreBeU32(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<uint32_t> {
  Status deserialize(const roo::byte* data, size_t len,
                     uint32_t& result) const {
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
  StaticSerialized<8> serialize(int64_t val) const {
    StaticSerialized<8> result;
    roo_io::StoreBeS64(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<int64_t> {
  Status deserialize(const roo::byte* data, size_t len, int64_t& result) const {
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
  StaticSerialized<8> serialize(uint64_t val) const {
    StaticSerialized<8> result;
    roo_io::StoreBeU64(val, result.data());
    return result;
  }
};

template <>
struct Deserializer<uint64_t> {
  Status deserialize(const roo::byte* data, size_t len,
                     uint64_t& result) const {
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
  SerializedByteArrayAdapter serialize(roo::string_view val) const {
    return SerializedByteArrayAdapter(
        reinterpret_cast<const roo::byte*>(val.data()), val.size());
  }
};

template <>
struct Deserializer<roo::string_view> {
  Status deserialize(const roo::byte* data, size_t len,
                     roo::string_view& result) const {
    result = roo::string_view(reinterpret_cast<const char*>(data), len);
    return roo_transport::kOk;
  }
};

// Pair.

template <typename T1, typename T2>
struct Serializer<std::pair<T1, T2>> {
  template <typename RandomItr>
  void serializeInto(const std::pair<T1, T2>& val, RandomItr& result) const {
    size_t pos1 = result.pos();
    result.seek(pos1 + 2);
    SerializeInto(val.first, result);
    size_t pos2 = result.pos();
    result.seek(pos1);
    roo_io::WriteBeU16(result, (uint16_t)(result.size() - (pos1 + 2)));
    result.seek(pos2 + 2);
    SerializeInto(val.second, result);
    size_t fin = result.pos();
    if (result.pos() > 65535) {
      result.fail(roo_transport::kInvalidArgument);
      return;
    }
    result.seek(pos2);
    roo_io::WriteBeU16(result, (uint16_t)(result.size() - (pos2 + 2)));
    result.seek(fin);
  }

  DynamicSerialized serialize(const std::pair<T1, T2>& val) const {
    DynamicSerialized result;
    serializeInto(val, result);
    return result;
  }
};

template <typename T1, typename T2>
struct Deserializer<std::pair<T1, T2>> {
  Status deserialize(const roo::byte* data, size_t len,
                     std::pair<T1, T2>& result) const {
    if (len < 4) {
      return roo_transport::kInvalidArgument;
    }
    uint16_t len1 = roo_io::LoadBeU16(data);
    if (len1 + 4 > len) {
      return roo_transport::kInvalidArgument;
    }
    Deserializer<T1> d1;
    Status status = d1.deserialize(data + 2, len1, result.first);
    if (status != roo_transport::kOk) {
      return status;
    }
    uint16_t len2 = roo_io::LoadBeU16(data + 2 + len1);
    if (len1 + 4 + len2 != len) {
      return roo_transport::kInvalidArgument;
    }
    Deserializer<T2> d2;
    status = d2.deserialize(data + 4 + len1, len2, result.second);
    if (status != roo_transport::kOk) {
      return status;
    }
    return roo_transport::kOk;
  }
};

// Tuple.

template <size_t index, typename RandomItr, typename... Types>
constexpr void SerializeTupleRecursive(const std::tuple<Types...>& t,
                                       RandomItr& result) {
  size_t pos1 = result.pos();
  result.seek(pos1 + 2);
  SerializeInto(std::get<index>(t), result);
  if (result.status() != kOk) return;
  size_t fin = result.pos();
  result.seek(pos1);
  roo_io::WriteBeU16(result, (uint16_t)(fin - (pos1 + 2)));
  result.seek(fin);
  if constexpr (index < sizeof...(Types) - 1) {
    SerializeTupleRecursive<index + 1>(t, result);
  }
}

template <typename... Types>
struct Serializer<std::tuple<Types...>> {
  template <typename RandomItr>
  constexpr void serializeInto(const std::tuple<Types...>& val,
                               RandomItr& result) const {
    SerializeTupleRecursive<0>(val, result);
  }

  DynamicSerialized serialize(const std::tuple<Types...>& val) const {
    DynamicSerialized result;
    SerializeTupleRecursive<0>(val, result);
    return result;
  }
};

template <size_t index, typename... Types>
constexpr Status DeserializeTupleRecursive(std::tuple<Types...>& t,
                                           const roo::byte* data, size_t len) {
  if (len < 2) {
    return roo_transport::kInvalidArgument;
  }
  uint16_t len1 = roo_io::LoadBeU16(data);
  if (len < 2 + len1) {
    return roo_transport::kInvalidArgument;
  }
  Deserializer<typename std::tuple_element<index, std::tuple<Types...>>::type>
      d;
  Status status = d.deserialize(data + 2, len1, std::get<index>(t));
  data += (2 + len1);
  len -= (2 + len1);
  if (status != kOk) {
    return status;
  }
  if constexpr (index == sizeof...(Types) - 1) {
    if (len != 0) {
      return kInvalidArgument;
    }
    return kOk;
  } else {
    return DeserializeTupleRecursive<index + 1>(t, data, len);
  }
}

template <typename... Types>
struct Deserializer<std::tuple<Types...>> {
  Status deserialize(const roo::byte* data, size_t len,
                     std::tuple<Types...>& result) const {
    return DeserializeTupleRecursive<0>(result, data, len);
  }
};

}  // namespace roo_transport
