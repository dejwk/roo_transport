#pragma once

#include "roo_transport/link/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_threads.h"
#include "roo_threads/condition_variable.h"
#include "roo_threads/mutex.h"

namespace roo_transport {
namespace internal {

/// Thread-safe notification primitive for waking the sender loop.
class OutgoingDataReadyNotification {
 public:
  /// Creates a notification object with no pending wakeup.
  OutgoingDataReadyNotification() : mutex_(), has_data_to_send_(false), cv_() {}

  /// Signals that there is data ready to send.
  void notify() {
    roo::unique_lock<roo::mutex> guard(mutex_);
    if (has_data_to_send_) return;
    has_data_to_send_ = true;
    // There is only one sender thread.
    cv_.notify_one();
  }

  /// Waits up to `micros` microseconds for a send notification.
  bool await(long micros) {
    roo::unique_lock<roo::mutex> guard(mutex_);
    bool result = cv_.wait_for(guard, roo_time::Micros(micros),
                               [this]() { return has_data_to_send_; });
    has_data_to_send_ = false;
    return result;
  }

 private:
  roo::mutex mutex_;
  bool has_data_to_send_;
  roo::condition_variable cv_;
};

}  // namespace internal
}  // namespace roo_transport

#endif  // ROO_USE_THREADS