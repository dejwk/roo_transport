#include "roo_transport/bidi_streaming/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_transport/bidi_streaming/internal/thread_safe/channel_input.h"

namespace roo_io {

void ChannelInput::close() {
  if (status_ != kOk && status_ != kEndOfStream) return;
  channel_->closeInput(my_stream_id_, status_);
  if (status_ != kOk && status_ != kEndOfStream) return;
  status_ = kClosed;
}

size_t ChannelInput::read(roo::byte* buf, size_t count) {
  if (count == 0 || status_ != kOk) return 0;
  return channel_->read(buf, count, my_stream_id_, status_);
  // // processor_.loop();
  // while (true) {
  //   for (int i = 0; i < 100; ++i) {
  //     size_t result = processor_.tryRead(buf, count);
  //     if (result > 0) {
  //       // if (count > result) {
  //       //   buf += result;
  //       //   count -= result;
  //       //   result += processor_.tryRead(buf, count);
  //       // }
  //       return result;
  //     }
  //     processor_.loop();
  //   }
  //   delay(1);
  // }
}

size_t ChannelInput::tryRead(roo::byte* buf, size_t count) {
  if (count == 0 || status_ != kOk) return 0;
  return channel_->tryRead(buf, count, my_stream_id_, status_);
}

void ChannelInput::onReceive(internal::ThreadSafeReceiver::RecvCb recv_cb) {
  channel_->onReceive(recv_cb, my_stream_id_, status_);
}

int ChannelInput::available() {
  if (status_ != kOk) return 0;
  return channel_->availableForRead(my_stream_id_, status_);
}

int ChannelInput::read() {
  if (status_ != kOk) return 0;
  roo::byte result;
  size_t count = channel_->tryRead(&result, 1, my_stream_id_, status_);
  if (count > 0) return (int)result;
  return -1;
}

int ChannelInput::peek() {
  if (status_ != kOk) return -1;
  int result = channel_->peek(my_stream_id_, status_);
  if (result >= 0) return result;
  return -1;
}

size_t ChannelInput::timedRead(roo::byte* buf, size_t count,
                               roo_time::Interval timeout) {
  roo_time::Uptime start = roo_time::Uptime::Now();
  size_t total = 0;
  if (status_ != kOk) return -1;
  while (count > 0) {
    for (int i = 0; i < 100; ++i) {
      size_t result = tryRead(buf, count);
      if (result == 0) {
        if (status_ != kOk) return -1;
        channel_->loop();
      } else {
        total += result;
        count -= result;
      }
      if (count == 0) return total;
    }
    if (roo_time::Uptime::Now() - start > timeout) break;
    delay(1);
  }
  return total;
}

}  // namespace roo_io

#endif  // ROO_USE_THREADS