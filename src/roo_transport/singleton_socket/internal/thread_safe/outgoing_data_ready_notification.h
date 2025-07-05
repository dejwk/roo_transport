#pragma once

#include "roo_transport/singleton_socket/internal/thread_safe/compile_guard.h"
#ifdef ROO_USE_THREADS

#include "roo_threads.h"
#include "roo_threads/condition_variable.h"
#include "roo_threads/mutex.h"

namespace roo_io {
namespace internal {

class OutgoingDataReadyNotification {
 public:
  OutgoingDataReadyNotification() : mutex_(), has_data_to_send_(false), cv_() {}

  void notify() {
    roo::unique_lock<roo::mutex> guard(mutex_);
    has_data_to_send_ = true;
    cv_.notify_all();
  }

  bool await(long micros) {
    roo::unique_lock<roo::mutex> guard(mutex_);
    cv_.wait_for(guard, roo_time::Micros(micros),
                 [this] { return has_data_to_send_; });
    if (has_data_to_send_) {
      has_data_to_send_ = false;
      return true;
    }
    return false;
  }

 private:
  roo::mutex mutex_;
  bool has_data_to_send_;
  roo::condition_variable cv_;
};

}  // namespace internal
}  // namespace roo_io

#endif  // ROO_USE_THREADS