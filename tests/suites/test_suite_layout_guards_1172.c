// CCCC_FLAGS: --testing
// -c=native/-m const-folds sizeof/_Alignof/member offsets at parse time,
// then emits the aggregate definition alongside those folded literals --
// correct only if the host compiler independently arrives at the same
// layout CCCC did. Where it doesn't, the emitted C compiled clean, ran, and
// wrote out of bounds, with no diagnostic on either side (#1170) -- and the
// native round-trip corpus couldn't see it: every affected test passed
// while doing exactly this.
//
// This is the systematic detector #1170 asked for: every emitted aggregate
// definition (struct/union/enum) now gets a `_Static_assert` guard next to
// it (sizeof, _Alignof, and __builtin_offsetof for each named non-bitfield
// member), converting any disagreement -- including ones nobody has found
// yet -- into a host compile error naming the type, instead of a silent
// out-of-bounds write.
//
// Deliberately its own file, same reason as test_suite_attributes_layout_
// 1129.c and test_suite_pragma_pack_1173.c: needs to round-trip cleanly
// through `tools/tests.py --native`, and every struct below is actually
// INSTANTIATED (a global variable) so the guard genuinely gets emitted --
// see serialize_layout_guards()'s own doc comment for why an unreferenced
// type gets no guard at all (and why test_suite_structs.c's own tc_bf1176_*
// structs, referenced only inside a compile-time-only _Static_assert, never
// trip one).
//
// Width-0 unnamed bit-fields are a DELIBERATE, PERMANENT divergence between
// gcc and clang (#1176 adopted gcc's rule: gcc/cccc size/align 8/4, clang
// 5/1) -- under a clang host, the guard for tc_layoutguard_zero_width below
// is a TRUE POSITIVE: the emitted C really is miscompiled there. This file
// is quarantined from the ordinary clang native corpus for exactly that
// reason (see NATIVE_SKIP_TESTS_CLANG, tools/testing/__init__.py) and
// verified separately under CCCC_NATIVE_CC=gcc-16, where it compiles clean.

#include <setjmp.h>
#include <time.h>

// An ordinary struct: every field of the guard (sizeof, _Alignof, and one
// __builtin_offsetof per member) is exercised here, cross-checked against
// both gcc-16 and clang while writing this test.
struct tc_layoutguard_plain {
    int  a;
    char b;
    long c;
}; // sizeof 16, _Alignof 8, offsetof(a)=0 offsetof(b)=4 offsetof(c)=8

struct tc_layoutguard_plain g_layoutguard_plain;

// #1176: the deliberate gcc/clang divergence -- see the file-header comment.
struct tc_layoutguard_zero_width {
    char c;
    int : 0;
    char d;
}; // gcc/cccc: sizeof 8, _Alignof 4 -- clang: sizeof 5, _Alignof 1

struct tc_layoutguard_zero_width g_layoutguard_zero_width;

// jmp_buf (include/setjmp.h: `typedef long long jmp_buf[40];`) is
// deliberately widened to a safe upper bound covering every supported host
// (#1054/#1059) -- CCCC's own folded size is intentionally NOT the real
// host's, so a guard on a struct containing one would fire on every host,
// gcc and clang alike. Excluded (type_contains_compiler_owned_layout()).
struct tc_layoutguard_with_jmp {
    int     x;
    jmp_buf env;
};

struct tc_layoutguard_with_jmp g_layoutguard_with_jmp;

// struct timespec is from_include and defers to the host's own real layout
// (type_layout_is_host_owned()) -- also excluded.
struct tc_layoutguard_with_ts {
    int             x;
    struct timespec t;
};

struct tc_layoutguard_with_ts g_layoutguard_with_ts;

[[cccc::test(return = 42)]]
int test_layout_guards_plain_struct(void) {
    _Static_assert(sizeof(struct tc_layoutguard_plain) == 16, "cccc");
    _Static_assert(_Alignof(struct tc_layoutguard_plain) == 8, "cccc");

    g_layoutguard_plain.a = 1;
    g_layoutguard_plain.b = 2;
    g_layoutguard_plain.c = 3;
    if (g_layoutguard_plain.a != 1 || g_layoutguard_plain.b != 2 ||
        g_layoutguard_plain.c != 3)
        return 1;

    return 42;
}

[[cccc::test(return = 42)]]
int test_layout_guards_zero_width_bitfield(void) {
    // #1176: pinned to gcc's own rule -- see the file-header comment for
    // why this is the one shape a clang host's guard genuinely rejects.
    _Static_assert(sizeof(struct tc_layoutguard_zero_width) == 8, "cccc");
    _Static_assert(_Alignof(struct tc_layoutguard_zero_width) == 4, "cccc");

    g_layoutguard_zero_width.c = 'x';
    g_layoutguard_zero_width.d = 'y';
    if (g_layoutguard_zero_width.c != 'x' || g_layoutguard_zero_width.d != 'y')
        return 1;

    return 42;
}

[[cccc::test(return = 42)]]
int test_layout_guards_excluded_members(void) {
    // Both structs above must still compile and behave normally under
    // -c=native even though neither gets a layout guard.
    g_layoutguard_with_jmp.x = 7;
    g_layoutguard_with_ts.x  = 9;
    if (g_layoutguard_with_jmp.x != 7 || g_layoutguard_with_ts.x != 9)
        return 1;

    return 42;
}
