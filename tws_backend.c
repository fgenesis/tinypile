/* Pick one or more */

#ifdef _WIN32
// we're good
#elif defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#else // whatever
#include <SDL_thread.h>
#endif

#define TWS_BACKEND_IMPLEMENTATION
#include "tws_backend.h"
