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
