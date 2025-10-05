#include "rand.h"

#include "roo_threads.h"
#include "roo_threads/mutex.h"
#include "roo_logging.h"
#include "esp_random.h"

namespace roo_transport {

int rand(void) {
  // rand() uses a global mutex internally, which interferes with freertos.
  return esp_random() & 0x7FFFFFFF;
}

}  // namespace roo_transport
