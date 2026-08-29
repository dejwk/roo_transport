#pragma once

#include <memory>
#include <vector>

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_backport/string_view.h"
#include "roo_io/data/read.h"
#include "roo_io/data/write.h"
#include "roo_io/memory/load.h"
#include "roo_io/memory/store.h"
#include "roo_transport/rpc/status.h"

namespace roo_transport {

/// Empty payload marker for RPCs with no request or response body.
struct Void {};

/// Primary template for RPC serializers.
template <typename T>
struct Serializer;

/// Primary template for RPC deserializers.
template <typename T>
struct Deserializer;

/// Serialized placeholder representing an empty payload.
struct NullSerialized {
  /// Returns serialization status.
  RpcStatus status() const { return kOk; }
  /// Returns a pointer to the serialized bytes.
  const roo::byte* data() const { return nullptr; }
  /// Returns the serialized byte count.
  size_t size() const { return 0; }
};

/// Serialized holder for small fixed-size payloads.
template <size_t N>
class StaticSerialized {
 public:
  /// Creates a fixed-size serialized buffer.
  StaticSerialized() = default;

  /// Returns serialization status.
  RpcStatus status() const { return kOk; }
  /// Returns a pointer to the serialized bytes.
  const roo::byte* data() const { return data_; }
  /// Returns the serialized byte count.
  size_t size() const { return N; }

  /// Returns mutable storage for writing serialized bytes.
  roo::byte* data() { return data_; }

 private:
  roo::byte data_[N];
};

/// Serialized holder for variable-sized payloads stored on heap.
class SimpleSerialized {
 public:
  /// Creates an empty successful serialization result.
  SimpleSerialized() : status_(kOk), data_(nullptr), size_(0) {}

  /// Creates a failed serialization result.
  SimpleSerialized(RpcStatus error)
      : status_(error), data_(nullptr), size_(0) {}

  /// Creates a successful serialization result backed by `data`.
  SimpleSerialized(std::unique_ptr<roo::byte[]> data, size_t size)
      : status_(kOk), data_(std::move(data)), size_(size) {}

  /// Returns serialization status.
  RpcStatus status() const { return status_; }
  /// Returns a pointer to the serialized bytes.
  const roo::byte* data() const { return data_.get(); }
  /// Returns the serialized byte count.
  size_t size() const { return size_; }

 private:
  RpcStatus status_;
  std::unique_ptr<roo::byte[]> data_;
  size_t size_;
};

/// Mutable serialized buffer supporting append and random-position writes.
class DynamicSerialized {
 public:
  /// Creates an empty successful serialization buffer.
  DynamicSerialized() : status_(kOk), data_(), pos_(0) {}

  //   DynamicSerialized(RpcStatus error)
  //       : status_(error), data_() {}

  //   DynamicSerialized(std::unique_ptr<roo::byte[]> data, size_t size)
  //       : status_(kOk), data_(std::move(data)), size_(size) {}

  /// Marks serialization as failed and clears accumulated data.
  void fail(RpcStatus status) {
    status_ = status;
    data_.clear();
    pos_ = 0;
  }

  /// Returns serialization status.
  RpcStatus status() const { return status_; }

  /// Returns a pointer to the serialized bytes.
  const roo::byte* data() const {
    return data_.empty() ? nullptr : &*data_.begin();
  }

  /// Returns the current write position.
  size_t pos() const { return pos_; }
  /// Returns the serialized byte count.
  size_t size() const { return data_.size(); }

  /// Returns whether the current write position is at end of buffer.
  bool eos() const { return pos_ >= data_.size(); }

  /// Iterator contract: writes one byte at current position and advances.
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

  /// Writes `count` bytes at the current position.
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

  /// Moves the current write position.
  void seek(size_t position) { pos_ = position; }

 private:
  RpcStatus status_;
  std::vector<roo::byte> data_;
  size_t pos_;
};

/// Default bridge implementing `SerializeInto()` via `serialize()`.
template <typename T, typename Itr, typename S = Serializer<T>, typename = void>
struct IntoSerializer {
  /// Serializes `val` into `output` using `Serializer<T>::serialize()`.
  void operator()(const T& val, Itr& output) const {
    Serializer<T> serializer;
    auto serialized = serializer.serialize(val);
    output.write(serialized.data(), serialized.size());
  }
};

/// Specialization using `Serializer<T>::serializeInto()` when available.
template <typename T, typename Itr>
struct IntoSerializer<T, Itr, Serializer<T>,
                      decltype(std::declval<Serializer<T>>().serializeInto(
                                   std::declval<const T&>(),
                                   std::declval<Itr&>()),
                               void())> {
  /// Serializes `val` into `output` using `Serializer<T>::serializeInto()`.
  void operator()(const T& val, Itr& output) const {
    Serializer<T> serializer;
    serializer.serializeInto(val, output);
  }
};

template <typename T, typename Itr>
void SerializeInto(const T& val, Itr& output) {
  IntoSerializer<T, Itr>()(val, output);
}

/// Simple serializers/deserializers for basic types are provided below.

/// Serializer for `Void`.

template <>
struct Serializer<Void> {
  /// Serializes an empty payload.
  NullSerialized serialize(const Void&) const { return NullSerialized(); }
};

/// Deserializer for `Void`.
template <>
struct Deserializer<Void> {
  /// Verifies that the payload is empty.
  RpcStatus deserialize(const roo::byte* data, size_t len, Void& result) const {
    if (len != 0) {
      return roo_transport::kInvalidArgument;
    }
    return roo_transport::kOk;
  }
};

/// Serializer for `bool`.

template <>
struct Serializer<bool> {
  /// Serializes `val` into one byte.
  StaticSerialized<1> serialize(bool val) const {
    StaticSerialized<1> result;
    roo_io::StoreU8(val, result.data());
    return result;
  }
};

/// Deserializer for `bool`.
template <>
struct Deserializer<bool> {
  /// Deserializes one byte into `result`.
  RpcStatus deserialize(const roo::byte* data, size_t len, bool& result) const {
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

/// Serializer for `int8_t`.

template <>
struct Serializer<int8_t> {
  /// Serializes `val` into one byte.
  StaticSerialized<1> serialize(int8_t val) const {
    StaticSerialized<1> result;
    roo_io::StoreS8(val, result.data());
    return result;
  }
};

/// Deserializer for `int8_t`.
template <>
struct Deserializer<int8_t> {
  /// Deserializes one byte into `result`.
  RpcStatus deserialize(const roo::byte* data, size_t len,
                        int8_t& result) const {
    if (len != 1) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadS8(data);
    return roo_transport::kOk;
  }
};

/// Serializer for `uint8_t`.

template <>
struct Serializer<uint8_t> {
  /// Serializes `val` into one byte.
  StaticSerialized<1> serialize(uint8_t val) const {
    StaticSerialized<1> result;
    roo_io::StoreU8(val, result.data());
    return result;
  }
};

/// Deserializer for `uint8_t`.
template <>
struct Deserializer<uint8_t> {
  /// Deserializes one byte into `result`.
  RpcStatus deserialize(const roo::byte* data, size_t len,
                        uint8_t& result) const {
    if (len != 1) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadU8(data);
    return roo_transport::kOk;
  }
};

/// Serializer for `int16_t`.

template <>
struct Serializer<int16_t> {
  /// Serializes `val` into two bytes.
  StaticSerialized<2> serialize(int16_t val) const {
    StaticSerialized<2> result;
    roo_io::StoreBeS16(val, result.data());
    return result;
  }
};

/// Deserializer for `int16_t`.
template <>
struct Deserializer<int16_t> {
  /// Deserializes two bytes into `result`.
  RpcStatus deserialize(const roo::byte* data, size_t len,
                        int16_t& result) const {
    if (len != 2) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadBeS16(data);
    return roo_transport::kOk;
  }
};

/// Serializer for `uint16_t`.

template <>
struct Serializer<uint16_t> {
  /// Serializes `val` into two bytes.
  StaticSerialized<2> serialize(uint16_t val) const {
    StaticSerialized<2> result;
    roo_io::StoreBeU16(val, result.data());
    return result;
  }
};

/// Deserializer for `uint16_t`.
template <>
struct Deserializer<uint16_t> {
  /// Deserializes two bytes into `result`.
  RpcStatus deserialize(const roo::byte* data, size_t len,
                        uint16_t& result) const {
    if (len != 2) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadBeU16(data);
    return roo_transport::kOk;
  }
};

/// Serializer for `int32_t`.

template <>
struct Serializer<int32_t> {
  /// Serializes `val` into four bytes.
  StaticSerialized<4> serialize(int32_t val) const {
    StaticSerialized<4> result;
    roo_io::StoreBeS32(val, result.data());
    return result;
  }
};

/// Deserializer for `int32_t`.
template <>
struct Deserializer<int32_t> {
  /// Deserializes four bytes into `result`.
  RpcStatus deserialize(const roo::byte* data, size_t len,
                        int32_t& result) const {
    if (len != 4) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadBeS32(data);
    return roo_transport::kOk;
  }
};

/// Serializer for `uint32_t`.

template <>
struct Serializer<uint32_t> {
  /// Serializes `val` into four bytes.
  StaticSerialized<4> serialize(uint32_t val) const {
    StaticSerialized<4> result;
    roo_io::StoreBeU32(val, result.data());
    return result;
  }
};

/// Deserializer for `uint32_t`.
template <>
struct Deserializer<uint32_t> {
  /// Deserializes four bytes into `result`.
  RpcStatus deserialize(const roo::byte* data, size_t len,
                        uint32_t& result) const {
    if (len != 4) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadBeU32(data);
    return roo_transport::kOk;
  }
};

/// Serializer for `int64_t`.

template <>
struct Serializer<int64_t> {
  /// Serializes `val` into eight bytes.
  StaticSerialized<8> serialize(int64_t val) const {
    StaticSerialized<8> result;
    roo_io::StoreBeS64(val, result.data());
    return result;
  }
};

/// Deserializer for `int64_t`.
template <>
struct Deserializer<int64_t> {
  /// Deserializes eight bytes into `result`.
  RpcStatus deserialize(const roo::byte* data, size_t len,
                        int64_t& result) const {
    if (len != 8) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadBeS64(data);
    return roo_transport::kOk;
  }
};

/// Serializer for `uint64_t`.

template <>
struct Serializer<uint64_t> {
  /// Serializes `val` into eight bytes.
  StaticSerialized<8> serialize(uint64_t val) const {
    StaticSerialized<8> result;
    roo_io::StoreBeU64(val, result.data());
    return result;
  }
};

/// Deserializer for `uint64_t`.
template <>
struct Deserializer<uint64_t> {
  /// Deserializes eight bytes into `result`.
  RpcStatus deserialize(const roo::byte* data, size_t len,
                        uint64_t& result) const {
    if (len != 8) {
      return roo_transport::kInvalidArgument;
    }
    result = roo_io::LoadBeU64(data);
    return roo_transport::kOk;
  }
};

/// Serialized view over an existing byte array.

class SerializedByteArrayAdapter {
 public:
  /// Creates an adapter over `data`.
  SerializedByteArrayAdapter(const roo::byte* data, size_t size)
      : data_(data), size_(size) {}

  /// Returns serialization status.
  RpcStatus status() const { return kOk; }
  /// Returns a pointer to the serialized bytes.
  const roo::byte* data() const { return data_; }
  /// Returns the serialized byte count.
  size_t size() const { return size_; }

 private:
  const roo::byte* data_;
  size_t size_;
};

/// Serializer for `roo::string_view`.
template <>
struct Serializer<roo::string_view> {
  /// Returns a zero-copy serialized view of `val`.
  SerializedByteArrayAdapter serialize(roo::string_view val) const {
    return SerializedByteArrayAdapter(
        reinterpret_cast<const roo::byte*>(val.data()), val.size());
  }
};

/// Deserializer for `roo::string_view`.
template <>
struct Deserializer<roo::string_view> {
  /// Creates a string view over the serialized bytes.
  RpcStatus deserialize(const roo::byte* data, size_t len,
                        roo::string_view& result) const {
    result = roo::string_view(reinterpret_cast<const char*>(data), len);
    return roo_transport::kOk;
  }
};

/// Serializes nested member with 16-bit big-endian length prefix.
template <typename T, typename RandomItr>
void SerializeMemberInto(const T& val, RandomItr& result) {
  size_t begin = result.pos();
  result.seek(begin + 2);
  SerializeInto(val, result);
  if (result.status() != kOk) return;
  size_t end = result.pos();
  size_t len = end - (begin + 2);
  if (len > 65535) {
    result.fail(roo_transport::kInvalidArgument);
    return;
  }
  result.seek(begin);
  roo_io::WriteBeU16(result, (uint16_t)len);
  result.seek(end);
}

template <typename T>
constexpr RpcStatus DeserializeMember(const roo::byte*& data, size_t& len,
                                      T& result) {
  if (len < 2) {
    return roo_transport::kInvalidArgument;
  }
  uint16_t member_len = roo_io::LoadBeU16(data);
  if (len - 2 < member_len) {
    return roo_transport::kInvalidArgument;
  }
  data += 2;
  len -= 2;
  Deserializer<T> d;
  RpcStatus status = d.deserialize(data, member_len, result);
  if (status != kOk) {
    return status;
  }
  data += member_len;
  len -= member_len;
  return kOk;
}

/// Serializer for `std::pair<T1, T2>`.

template <typename T1, typename T2>
struct Serializer<std::pair<T1, T2>> {
  template <typename RandomItr>
  /// Serializes `val` into `result`.
  void serializeInto(const std::pair<T1, T2>& val, RandomItr& result) const {
    SerializeMemberInto(val.first, result);
    if (result.status() != kOk) return;
    SerializeMemberInto(val.second, result);
  }

  /// Serializes `val` into a dynamically sized buffer.
  DynamicSerialized serialize(const std::pair<T1, T2>& val) const {
    DynamicSerialized result;
    serializeInto(val, result);
    return result;
  }
};

/// Deserializer for `std::pair<T1, T2>`.
template <typename T1, typename T2>
struct Deserializer<std::pair<T1, T2>> {
  /// Deserializes `data` into `result`.
  RpcStatus deserialize(const roo::byte* data, size_t len,
                        std::pair<T1, T2>& result) const {
    RpcStatus status;
    status = DeserializeMember(data, len, result.first);
    if (status != roo_transport::kOk) {
      return status;
    }
    status = DeserializeMember(data, len, result.second);
    if (status != roo_transport::kOk) {
      return status;
    }
    if (len != 0) {
      return roo_transport::kInvalidArgument;
    }
    return roo_transport::kOk;
  }
};

/// Serializer for `std::tuple<Types...>`.

template <size_t index, typename RandomItr, typename... Types>
constexpr void SerializeTupleRecursive(const std::tuple<Types...>& t,
                                       RandomItr& result) {
  SerializeMemberInto(std::get<index>(t), result);
  if (result.status() != kOk) return;
  if constexpr (index < sizeof...(Types) - 1) {
    SerializeTupleRecursive<index + 1>(t, result);
  }
}

template <typename... Types>
struct Serializer<std::tuple<Types...>> {
  template <typename RandomItr>
  /// Serializes `val` into `result`.
  constexpr void serializeInto(const std::tuple<Types...>& val,
                               RandomItr& result) const {
    SerializeTupleRecursive<0>(val, result);
  }

  /// Serializes `val` into a dynamically sized buffer.
  DynamicSerialized serialize(const std::tuple<Types...>& val) const {
    DynamicSerialized result;
    SerializeTupleRecursive<0>(val, result);
    return result;
  }
};

template <size_t index, typename... Types>
constexpr RpcStatus DeserializeTupleRecursive(std::tuple<Types...>& t,
                                              const roo::byte* data,
                                              size_t len) {
  RpcStatus status = DeserializeMember(data, len, std::get<index>(t));
  if (status != kOk) {
    return status;
  }
  if constexpr (index < sizeof...(Types) - 1) {
    return DeserializeTupleRecursive<index + 1>(t, data, len);
  }
  if (len != 0) {
    return kInvalidArgument;
  }
  return kOk;
}

/// Deserializer for `std::tuple<Types...>`.
template <typename... Types>
struct Deserializer<std::tuple<Types...>> {
  /// Deserializes `data` into `result`.
  RpcStatus deserialize(const roo::byte* data, size_t len,
                        std::tuple<Types...>& result) const {
    return DeserializeTupleRecursive<0>(result, data, len);
  }
};

}  // namespace roo_transport
