// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __cccc_environ_ptr\(void\) \{ return &environ; \}
//
// #1139: environ (include/unistd.h) is `#define environ
// (*__cccc_environ_ptr())`, an accessor for a VM-only cfunc
// (src/stdlib/posix_io.c) -- every sibling accessor (__cccc_stdin,
// __cccc_errno_ptr, __cccc_optarg_ptr, etc) already had an entry in
// native_accessor_shims (src/serialize.c), but __cccc_environ_ptr didn't,
// so a guest read of `environ` reached -c=native's output as a call to an
// entirely undeclared identifier. Fixed by adding it to that table; the
// shim's own leading `#undef environ` is load-bearing -- without it, the
// shim's own `extern char **environ;` would (once CCCC's own unistd.h is
// in scope, the -I./include replay-forwarding case) expand right back
// through the same macro into nonsense syntax, the identical
// infinite-recursion-shaped trap FLT_ROUNDS/isnan/MB_CUR_MAX each need
// their own workaround for in that same table.
#include <unistd.h>

int main(void) {
    char **e = environ;
    (void)e;
    return 42;
}
