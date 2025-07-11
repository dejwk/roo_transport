#include "roo_transport/bidi_streaming/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_transport/bidi_streaming/internal/thread_safe/channel_output.h"

namespace roo_io {

size_t ChannelOutput::write(const roo::byte* buf, size_t count) {
  if (status_ != kOk) return 0;
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

size_t ChannelOutput::tryWrite(const roo::byte* buf, size_t count) {
  if (status_ != kOk) return 0;
  return channel_->tryWrite(buf, count, my_stream_id_, status_);
}

int ChannelOutput::availableForWrite() {
  if (status_ != kOk) return 0;
  return channel_->availableForWrite(my_stream_id_, status_);
}

void ChannelOutput::flush() {
  if (status_ != kOk) return;
  channel_->flush(my_stream_id_, status_);
}

void ChannelOutput::close() {
  if (status_ != kOk) return;
  channel_->close(my_stream_id_, status_);
  if (status_ == kOk) status_ = kClosed;
}

}  // namespace roo_io

#endif  // ROO_USE_THREADS