// Regression test for #1095, #1031's own residual: a `sizeof`/`_Alignof`
// of a `from_include` type (e.g. `struct statfs`) re-materializes textually
// under -c=native only when it survives as a bare expression node by the
// time serialize_expr() runs (#1031's own fix). Every other const_expr()/
// eval() consumer kept only the folded int64_t and stayed folded against
// CCCC's own (possibly stale/wrong) guest projection -- #1095 closes three
// of those: array dimensions, `case` labels, and enum values (see
// man/HEADERS.md and man/COVERAGE.md for the residual that's still open:
// bitfield widths, `_Static_assert`, and an initialized global's byte
// image, plus an array dimension appearing on a struct/union member).
//
// The enum and case-label checks are plain value comparisons against an
// ordinary bare `sizeof(struct statfs)` expression, which #1031 already
// re-materializes correctly -- deterministic, no memory-layout assumption
// needed: pre-#1095 the enum/case constant folds to CCCC's own guest
// projection while the right-hand `sizeof` re-materializes to the real
// host's, so they disagree under -c=native even though both read the same
// (consistent) folded value on the VM.
//
// The array-dimension check instead exercises a fixed-size *declarator*
// (`char buf[sizeof(...)]`, #1095's own new case -- test_sys_mount_statfs.c's
// own #1031 fix only covers `malloc(sizeof(...))`, an expression): a plain
// local array sized off the real host layout, handed to the real host
// statfs(). Deliberately not a poison-tail canary the way that test's
// malloc'd buffer gets one -- a stack local has no reliable adjacency
// guarantee to build one on (unlike two members of the same struct, and a
// struct MEMBER's own array dimension is deliberately excluded from
// re-materialization, see man/COVERAGE.md's bitfield-width reasoning, so
// wrapping `buf` in a struct here would just test the fold path, not the
// fix). Pre-#1095, `buf` stays CCCC's own guest-folded ~56 bytes even
// under -c=native, so the real host statfs() (~2100 bytes on macOS)
// overruns the stack past it -- in practice this reliably crashes (a
// clearly distinct failure from a clean exit 42) rather than silently
// succeeding, since the real write is roughly 40x the buffer's folded
// size; the deterministic half of this file's coverage is the enum/case
// checks above and the -m text assertion in
// tools/comptime_native_smoke.py.
#include <sys/mount.h>

enum { N = sizeof(struct statfs) };

static char g_uninit[sizeof(struct statfs)]; // no initializer -- eligible

int main(void) {
    if (N != (int)sizeof(struct statfs))
        return 1;

    unsigned long n = sizeof(struct statfs);
    switch (n) {
        case sizeof(struct statfs):
            break;
        default:
            return 2;
    }

    // Sanity: the uninitialized global at least exists and is nameable at
    // its real size (a stale ~56-byte guest projection would still link
    // and run, so this isn't load-bearing on its own -- the real assertion
    // is the -m text check in tools/comptime_native_smoke.py).
    if (sizeof(g_uninit) == 0)
        return 3;

    char           buf[sizeof(struct statfs)];
    struct statfs *sb = (struct statfs *)buf;
    if (statfs("/", sb) != 0)
        return 4;
    if (sb->f_bsize == 0)
        return 5;

    return 42;
}
