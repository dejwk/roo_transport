#include "roo_transport/link/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_transport/link/link_output_stream.h"

namespace roo_transport {

LinkOutputStream::LinkOutputStream(LinkOutputStream&& other) {
  channel_ = other.channel_;
  my_stream_id_ = other.my_stream_id_;
  status_ = other.status_;
  other.channel_ = nullptr;
  other.my_stream_id_ = 0;
  other.status_ = roo_io::kClosed;
}

LinkOutputStream& LinkOutputStream::operator=(LinkOutputStream&& other) {
  if (this != &other) {
    channel_ = other.channel_;
    my_stream_id_ = other.my_stream_id_;
    status_ = other.status_;
    other.channel_ = nullptr;
    other.my_stream_id_ = 0;
    other.status_ = roo_io::kClosed;
  }
  return *this;
}

size_t LinkOutputStream::write(const roo::byte* buf, size_t count) {
  if (status_ != roo_io::kOk) return 0;
  if (count == 0) return 0;
  return channel_->write(buf, count, my_stream_id_, status_);
  // while (true) {
  //   for (int i = 0; i < 100; ++i) {
  //     size_t result = processor_.tryWrite(buf, count);
  //     if (result > 0) {
  //       // if (count > result) {
  //       //   buf += result;
  //       //   count -= result;
  //       //   result += processor_.tryWrite(buf, count);
  //       // }
  //       return result;
  //     }
  //     processor_.loop();
  //   }
  //   delay(1);
  // }
}

size_t LinkOutputStream::tryWrite(const roo::byte* buf, size_t count) {
  if (status_ != roo_io::kOk) return 0;
  return channel_->tryWrite(buf, count, my_stream_id_, status_);
}

size_t LinkOutputStream::availableForWrite() {
  if (status_ != roo_io::kOk) return 0;
  return channel_->availableForWrite(my_stream_id_, status_);
}

void LinkOutputStream::flush() {
  if (status_ != roo_io::kOk) return;
  channel_->flush(my_stream_id_, status_);
}

void LinkOutputStream::close() {
  if (status_ != roo_io::kOk) return;
  channel_->close(my_stream_id_, status_);
  if (status_ == roo_io::kOk) status_ = roo_io::kClosed;
}

}  // namespace roo_transport

#endif  // ROO_USE_THREADS