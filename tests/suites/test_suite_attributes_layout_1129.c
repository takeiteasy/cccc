// CCCC_FLAGS: --testing
// #1129: __attribute__((packed))/__attribute__((aligned(N))) and a member's
// _Alignas(N) were retained on Type (is_packed, align)/Member (align) but
// never re-emitted by -c=native/-m, so a struct's native layout silently
// diverged from the VM's -- see the admissibility-rule discussion in
// man/COVERAGE.md. Every assertion here is sizeof/offsetof/_Alignof, so the
// same numbers a real GCC/clang would compute for the emitted C are checked
// directly, not just VM-internal consistency.
//
// Deliberately its own file, not folded into test_suite_attributes.c: that
// suite also declares __attribute__((test_setup))/((test_teardown)) hooks
// elsewhere in the same file, which makes the *whole file* native-skipped
// (#1033 v1: no fork-safe native equivalent for a `once` setup hook) --
// these tests need to actually round-trip through `tools/tests.py --native`
// to be worth anything, so they can't share a file with that gate.

#include "stddef.h"

struct Packed1129 {
    char a;
    int  b;
} __attribute__((packed));

[[cccc::test(return = 42)]]
int test_packed_struct_layout(void) {
    if (sizeof(struct Packed1129) != 5)
        return 1;
    if (offsetof(struct Packed1129, b) != 1)
        return 2;
    return 42;
}

struct Aligned1129Inner {
    char a;
} __attribute__((aligned(16)));

struct Aligned1129Outer {
    struct Aligned1129Inner x;
    char                    t;
};

[[cccc::test(return = 42)]]
int test_type_aligned_struct_layout(void) {
    if (_Alignof(struct Aligned1129Inner) != 16)
        return 1;
    if (offsetof(struct Aligned1129Outer, t) != 16)
        return 2;
    return 42;
}

struct MemberAlignas1129 {
    char a;
    _Alignas(16) int b;
};

[[cccc::test(return = 42)]]
int test_member_alignas_struct_layout(void) {
    if (offsetof(struct MemberAlignas1129, b) != 16)
        return 1;
    if (sizeof(struct MemberAlignas1129) != 32)
        return 2;
    return 42;
}

struct PackedAligned1129 {
    char a;
    int  b;
} __attribute__((packed, aligned(8)));

[[cccc::test(return = 42)]]
int test_packed_aligned_combo_layout(void) {
    if (sizeof(struct PackedAligned1129) != 8)
        return 1;
    if (_Alignof(struct PackedAligned1129) != 8)
        return 2;
    if (offsetof(struct PackedAligned1129, b) != 1)
        return 3;
    return 42;
}

union PackedUnion1129 {
    char a;
    int  b;
} __attribute__((packed));

[[cccc::test(return = 42)]]
int test_packed_union_layout(void) {
    // A union's own size is already max(member sizes) with no interior
    // padding to remove, so `packed` here only needs to not corrupt
    // that -- the meaningful case above is the struct one.
    if (sizeof(union PackedUnion1129) != 4)
        return 1;
    return 42;
}

// Anonymous (tagless, no typedef) packed struct -- exercises
// serialize_anon_aggregate() rather than serialize_struct_def(), the other
// of the two near-identical emitters #1129 touched.
struct AnonPackedHolder1129 {
    struct {
        char a;
        int  b;
    } __attribute__((packed)) inner;
    char                      t;
};

[[cccc::test(return = 42)]]
int test_anon_packed_aggregate_layout(void) {
    if (sizeof(((struct AnonPackedHolder1129 *)0)->inner) != 5)
        return 1;
    if (offsetof(struct AnonPackedHolder1129, t) != 5)
        return 2;
    return 42;
}

// #1160: __attribute__((aligned(N))) in declarator-suffix position on a
// struct member -- distinct from Aligned1129Inner above, which is
// __attribute__((aligned(N))) on the *struct itself*, and from
// MemberAlignas1129 above, which is a member's own _Alignas(N). Neither of
// those exercises the bug: attribute_list()'s "aligned" handler wrote
// Type.align only when passed a real `ty`, and every declarator-position
// call site (declspec/declarator prefix and suffix) passes ty==NULL, so the
// constant was parsed and silently discarded. Every number here is checked
// against real GCC/clang first (see the ticket's own repro and this file's
// header comment on why that matters).
struct MemberGnuAligned1160 {
    char a;
    int  b __attribute__((aligned(16)));
};

[[cccc::test(return = 42)]]
int test_member_gnu_aligned_suffix_layout(void) {
    if (offsetof(struct MemberGnuAligned1160, b) != 16)
        return 1;
    if (sizeof(struct MemberGnuAligned1160) != 32)
        return 2;
    if (_Alignof(struct MemberGnuAligned1160) != 16)
        return 3;
    return 42;
}

// Same request, declspec-prefix spelling (`__attribute__((aligned(16)))
// int b;`) -- a different parser path (declspec()'s VarAttr, not
// declarator()'s Type.decl_align) that must reach the same result.
struct MemberGnuAlignedPrefix1160 {
    char                             a;
    __attribute__((aligned(16))) int b;
};

[[cccc::test(return = 42)]]
int test_member_gnu_aligned_prefix_layout(void) {
    if (offsetof(struct MemberGnuAlignedPrefix1160, b) != 16)
        return 1;
    return 42;
}

// C23 [[gnu::aligned(N)]] spelling, both declarator-suffix and
// declspec-prefix position -- c23_attribute_list() had no "aligned" case at
// all before #1160.
struct MemberC23Aligned1160 {
    char a;
    int  b [[gnu::aligned(16)]];
};

struct MemberC23AlignedPrefix1160 {
    char                     a;
    [[gnu::aligned(16)]] int b;
};

[[cccc::test(return = 42)]]
int test_member_c23_aligned_layout(void) {
    if (offsetof(struct MemberC23Aligned1160, b) != 16)
        return 1;
    if (offsetof(struct MemberC23AlignedPrefix1160, b) != 16)
        return 2;
    return 42;
}

// A declarator-suffix attribute must attach only to the declarator it
// follows, not leak onto a sibling sharing the same declspec basety
// (verified against gcc-16: `c` stays at its natural offset 20, not 32).
struct MemberAlignedIsolation1160 {
    char a;
    int  b __attribute__((aligned(16))), c;
};

[[cccc::test(return = 42)]]
int test_member_aligned_isolation_layout(void) {
    if (offsetof(struct MemberAlignedIsolation1160, b) != 16)
        return 1;
    if (offsetof(struct MemberAlignedIsolation1160, c) != 20)
        return 2;
    return 42;
}

// GNU aligned(N) is a floor: it can only raise alignment (only `packed`
// lowers it), unlike _Alignas which can request less than natural
// alignment. Verified against gcc-16: both members stay at their natural
// offset/alignment.
struct MemberAlignedNeverLowers1160 {
    char      a;
    long long x __attribute__((aligned(2)));
};

struct MemberAlignedNeverLowers1160b {
    char a;
    int  b __attribute__((aligned(1)));
};

[[cccc::test(return = 42)]]
int test_member_aligned_never_lowers_layout(void) {
    if (offsetof(struct MemberAlignedNeverLowers1160, x) != 8)
        return 1;
    if (offsetof(struct MemberAlignedNeverLowers1160b, b) != 4)
        return 2;
    return 42;
}

// Bare __attribute__((aligned)) (no argument) requests maximum useful
// alignment -- man/COVERAGE.md already documented this as supported, but
// the declarator-suffix path silently dropped it like every other
// declarator-position aligned(N) request.
struct MemberBareAligned1160 {
    char a;
    int  b __attribute__((aligned));
};

[[cccc::test(return = 42)]]
int test_member_bare_aligned_layout(void) {
    if (_Alignof(struct MemberBareAligned1160) != 16)
        return 1;
    return 42;
}

// File-scope variable, the other declarator-position case #1160 covers --
// not member-specific. _Alignof takes a type, not an object, so check the
// object's actual address instead.
int global_gnu_aligned_1160 __attribute__((aligned(64)));

[[cccc::test(return = 42)]]
int test_global_gnu_aligned_layout(void) {
    if ((unsigned long)(void *)&global_gnu_aligned_1160 % 64 != 0)
        return 1;
    return 42;
}

// Pre-tag C23 attributes on a struct/union definition itself (as opposed to
// a member) -- gcc-16 verified these ARE honored in this position (unlike
// the trailing post-'}' position, where gcc silently ignores
// [[gnu::aligned(N)]]/[[gnu::packed]] -- ticket #1160's investigation found
// this, and c23_attribute_list_ex()'s `allow_ty_align` parameter exists to
// keep cccc matching that split).
struct [[gnu::packed]] PackedC231160 {
    char a;
    int  b;
};

struct [[gnu::aligned(16)]] AlignedC231160 {
    char a;
};

[[cccc::test(return = 42)]]
int test_c23_pretag_packed_aligned_layout(void) {
    if (sizeof(struct PackedC231160) != 5)
        return 1;
    if (_Alignof(struct AlignedC231160) != 16)
        return 2;
    return 42;
}
