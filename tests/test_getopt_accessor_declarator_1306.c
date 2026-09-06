// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: defined as a macro by a bundled system header
// A parameter, local, or struct member named after a bundled host-accessor
// macro (`optind` here, also `optarg`/`opterr`/`optopt`/`optreset` from
// <getopt.h>, `errno`, `stdin`/`stdout`/`stderr`) is macro-expanded even in
// declarator position: `int optind` parses as `int (*__cccc_optind_ptr)(void)`,
// a bogus function pointer, and any call through it jumps to an integer
// (SIGBUS natively, "invalid indirect call target" under the VM). This only
// bites when CCCC compiles its own source (self-hosting) -- a normal build
// sees the real system headers. Diagnose it at parse time with a clear
// message instead.

#include <getopt.h>

static int pick(int optind, int fallback) {
    return optind > 0 ? optind : fallback;
}

int main(void) {
    return pick(42, 7);
}
