#pragma once

#include "roo_threads.h"
#include "roo_threads/mutex.h"

#ifndef ROO_USE_THREADS
#if (defined ESP_PLATFORM || defined ROO_THREADS_USE_FREERTOS || \
     defined __linux__)
#define ROO_USE_THREADS
#endif
#endif
