// Regression test for #1098: a `_Static_assert`/`static_assert` whose
// condition folds a `sizeof`/`_Alignof` of a `from_include` type (e.g.
// `struct statfs`) is evaluated against CCCC's own type projection at
// parse time -- a failing assertion is a compile error, so only a passing
// one ever reaches the serializer. Before this fix, the serializer never
// emitted `_Static_assert` at all (see #1031/#1095's own residual writeup
// in man/HEADERS.md and man/NATIVE.md), so a host whose real layout
// would fail the same check compiled anyway. `-c=native` now re-emits the
// assert -- gated on the condition actually depending on a host-owned
// layout (type_layout_is_host_owned()) AND the assert being written in a
// command-line input file, so one of CCCC's own bundled headers' own
// per-platform layout asserts (include/sys/stat.h, signal.h, fts.h,
// aio.h, etc.) is never re-emitted against the wrong host.
//
// The condition below (`>= 8`, not `==`) is deliberately true against
// both CCCC's own ~56-byte guest projection of `struct statfs` and the
// real host's (much larger) one, so this test exercises the RE-EMISSION
// itself (see tools/comptime_native_smoke.py's own text assertion on `-m`
// output for that), not a projection-vs-real-layout mismatch -- if the
// gate were somehow bypassed and the wrong value got re-checked, this
// program would still pass either way, which is intentional: a test that
// only passes when the fix DOESN'T do its job would be a bad regression
// guard for anyone rerunning this on the VM path (no serializer involved
// at all there).
#include <sys/mount.h>

_Static_assert(sizeof(struct statfs) >= 8,
               "struct statfs must be at least 8 bytes");

int main(void) {
    // Block-scope form, same gate.
    static_assert(sizeof(struct statfs) >= 8,
                  "struct statfs must be at least 8 bytes (block scope)");

    // Negative half: an ordinary compile-time-only assert (no from_include
    // type involved) must NOT trigger re-emission -- see the smoke test's
    // own text assertion; this is here as a compile-time sanity check that
    // adding the fix didn't somehow break ordinary asserts.
    _Static_assert(sizeof(int) == 4, "int must be 4 bytes");

    return 42;
}
