/* ---- Begin compile config ---- */

// This is a safe upper limit for stack allocations.
// Especially on windows, UTF-8 to wchar_t conversion requires some temporary memory,
// and that's best allocated from the stack to keep the code free of heap allocations.
// This value limits the length of paths that can be processed by this library.
#define TIO_MAX_STACK_ALLOC 0x8000

// Print some things in debug mode. For debugging internals.
#define TIO_ENABLE_DEBUG_TRACE

/* ---- End compile config ---- */