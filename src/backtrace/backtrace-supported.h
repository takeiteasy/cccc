/* backtrace-supported.h -- Hand-written for CCCC (no autoconf).
   Indicates that libbacktrace is supported on macOS and Linux. */

/* Backtrace is supported on all CCCC target platforms. */
#define BACKTRACE_SUPPORTED 1

/* We use the mmap allocator (mmap.c + mmapio.c), not malloc. */
#define BACKTRACE_USES_MALLOC 0

/* Thread support via __atomic (state.c). */
#define BACKTRACE_SUPPORTS_THREADS 1

/* Variable address lookup (syminfo). */
#define BACKTRACE_SUPPORTS_DATA 1
