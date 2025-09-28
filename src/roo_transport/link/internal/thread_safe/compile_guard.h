#pragma once

#ifndef ROO_USE_THREADS
#if (defined ESP32 || defined __linux__)
#define ROO_USE_THREADS
#endif
#endif
