// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __builtin_memset\(&st, 0, sizeof\(st\)\)
// CCCC_REJECT_STDOUT: __opaque
// CCCC_C4_SKIP: -m serializer-shape check, not a bytecode round-trip
//
// #1103: `mbstate_t st = {0};` lowers to
// ND_COMMA(ND_MEMZERO(st), st.__opaque[0] = 0) -- `__opaque` is CCCC's own
// reserved-storage projection for this from_include type
// (include/wchar.h), invented to hide a platform-varying internal layout
// behind a fixed byte buffer with no counterpart in the real host struct.
// Printed verbatim under -c=native, where the real host <wchar.h> is in
// scope (macOS's own __mbstate_t is a union of __mbstate8/_mbstateL, no
// __opaque member at all), this was "no member named '__opaque' in
// '__mbstate_t'". Fixed by dropping every zero-store into `__opaque`
// through a `{0}`-initialized host-owned-layout local's own leading
// ND_MEMZERO -- already redundant with it -- while leaving a store through
// any of a host-owned type's REAL, POSIX-named members (struct timespec's
// tv_sec/tv_nsec, etc.) untouched; see
// expr_roots_at_opaque_member()'s own comment (src/serialize.c) for why
// the fix is scoped that narrowly, and
// tools/comptime_native_smoke.py's own `struct timespec nap = {0,
// 20000000}` case for the regression that a broader "any host-owned
// member" version of this fix introduced and this scoping avoids.
#include <wchar.h>

int main(void) {
    mbstate_t st = {0};
    (void)st;
    return 42;
}
