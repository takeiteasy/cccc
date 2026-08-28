// CCCC_FLAGS: --testing
// #pragma pack(N) was parsed and accepted, but struct/union layout was
// computed as though it were absent while the pragma was auto-captured and
// re-emitted VERBATIM (and hoisted to the top of the file, ahead of every
// struct) into -c=native/-m output, where the host compiler *does* honour
// it -- the folded sizeof/offsets and the host's real layout disagreed, and
// the emitted C wrote out of bounds (confirmed under AddressSanitizer on
// both gcc-16 and clang, see man/NATIVE.md's prior #pragma pack row).
// #pragma pack(N) is now fully honoured: parsed into a push/pop stack
// (mirroring #pragma GCC diagnostic push/pop's own mechanism), applied
// during struct_decl/union_decl's layout computation
// (src/parse_types.c), and re-emitted as #pragma pack(push, N)/pop wrapped
// tightly around just the affected definition (src/serialize_type.c) --
// not hoisted, not verbatim.
//
// Every assertion here is sizeof/offsetof/_Alignof, cross-checked directly
// against gcc-16 and clang while writing these tests, not just VM-internal
// consistency -- see #1129's test suite for the same discipline. Deliberately
// its own file for the same reason as test_suite_attributes_layout_1129.c:
// needs to round-trip cleanly through `tools/tests.py --native`.

#include "stddef.h"

// --- The ticket's own repro: pack(1) fully packs, and the array stride
// (the half that produced the actual out-of-bounds write) is checked too,
// not just sizeof. ---

#pragma pack(1)
struct Pack1_1173 {
    char c;
    int  a;
};
#pragma pack()

[[cccc::test(return = 42)]]
int test_pack1_basic_layout(void) {
    if (sizeof(struct Pack1_1173) != 5)
        return 1;
    if (offsetof(struct Pack1_1173, a) != 1)
        return 2;
    if (_Alignof(struct Pack1_1173) != 1)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_pack1_array_stride(void) {
    struct Pack1_1173 arr[3];
    arr[0].a = 10;
    arr[1].a = 20;
    arr[2].a = 30;
    char *base = (char *)arr;
    if ((char *)&arr[1] - base != 5)
        return 1;
    if ((char *)&arr[2] - base != 10)
        return 2;
    if (arr[0].a != 10 || arr[1].a != 20 || arr[2].a != 30)
        return 3;
    return 42;
}

// --- Caps at various N, including a no-op (N exceeds natural alignment). ---

#pragma pack(2)
struct Pack2_1173 {
    char c;
    int  a;
};
#pragma pack()

[[cccc::test(return = 42)]]
int test_pack2_cap(void) {
    if (sizeof(struct Pack2_1173) != 6)
        return 1;
    if (offsetof(struct Pack2_1173, a) != 2)
        return 2;
    if (_Alignof(struct Pack2_1173) != 2)
        return 3;
    return 42;
}

#pragma pack(4)
struct Pack4_1173 {
    char c;
    int  a;
};
#pragma pack()

[[cccc::test(return = 42)]]
int test_pack4_cap(void) {
    if (sizeof(struct Pack4_1173) != 8)
        return 1;
    if (offsetof(struct Pack4_1173, a) != 4)
        return 2;
    return 42;
}

#pragma pack(16)
struct PackNoop1173 {
    char c;
    int  a;
};
#pragma pack()

// N (16) exceeds every member's natural alignment here, so this must be
// byte-identical to an ordinary, unpacked struct.
[[cccc::test(return = 42)]]
int test_pack_exceeds_natural_is_noop(void) {
    if (sizeof(struct PackNoop1173) != 8)
        return 1;
    if (offsetof(struct PackNoop1173, a) != 4)
        return 2;
    if (_Alignof(struct PackNoop1173) != 4)
        return 3;
    return 42;
}

// --- push/pop, including nesting, restores the previous value exactly. ---

#pragma pack(push, 2)
struct PushA1173 {
    char c;
    int  a;
};
#pragma pack(push, 1)
struct PushB1173 {
    char c;
    int  a;
};
#pragma pack(pop)
struct PushC1173 {
    char c;
    int  a;
};
#pragma pack(pop)
struct PushD1173 {
    char c;
    int  a;
};

[[cccc::test(return = 42)]]
int test_pack_push_pop_nested(void) {
    if (sizeof(struct PushA1173) != 6)
        return 1;
    if (sizeof(struct PushB1173) != 5)
        return 2;
    if (sizeof(struct PushC1173) != 6) // restored to the pack(push, 2) frame
        return 3;
    if (sizeof(struct PushD1173) != 8) // restored to no pack at all
        return 4;
    return 42;
}

// --- Named push/pop (GCC/MSVC's pack(push, ident[, N])/pack(pop, ident)). ---

#pragma pack(push, mypack1173, 1)
struct NamedPush1173 {
    char c;
    int  a;
};
#pragma pack(pop, mypack1173)
struct AfterNamedPop1173 {
    char c;
    int  a;
};

[[cccc::test(return = 42)]]
int test_pack_named_push_pop(void) {
    if (sizeof(struct NamedPush1173) != 5)
        return 1;
    if (sizeof(struct AfterNamedPop1173) != 8)
        return 2;
    return 42;
}

// --- A struct declared before the pragma, and one after the matching
// reset, are both unaffected -- the exact case the pre-fix auto-capture
// hoisting got wrong (it replayed every #pragma pack line at the top of
// the file, ahead of every struct, in reverse order). ---

struct BeforePack1173 {
    char c;
    int  a;
};
#pragma pack(1)
struct DuringPack1173 {
    char c;
    int  a;
};
#pragma pack()
struct AfterPack1173 {
    char c;
    int  a;
};

[[cccc::test(return = 42)]]
int test_pack_declared_before_and_after_unaffected(void) {
    if (sizeof(struct BeforePack1173) != 8)
        return 1;
    if (sizeof(struct DuringPack1173) != 5)
        return 2;
    if (sizeof(struct AfterPack1173) != 8)
        return 3;
    return 42;
}

// --- Composed with an explicit member aligned(N): unlike
// __attribute__((packed)) (#1163, where an explicit request always wins
// outright), #pragma pack(N) caps EVERY contribution, explicit included --
// confirmed directly against gcc-16/clang while writing this test. ---

#pragma pack(2)
struct PackAlignedCombo1173 {
    char c;
    int  a __attribute__((aligned(16)));
};
#pragma pack()

[[cccc::test(return = 42)]]
int test_pack_caps_explicit_aligned_member(void) {
    if (sizeof(struct PackAlignedCombo1173) != 6)
        return 1;
    if (_Alignof(struct PackAlignedCombo1173) != 2)
        return 2;
    if (offsetof(struct PackAlignedCombo1173, a) != 2)
        return 3;
    return 42;
}

// --- Packed unions and bit-fields under pack(N). ---

#pragma pack(2)
union PackUnion1173 {
    char c;
    int  a;
};
#pragma pack()

[[cccc::test(return = 42)]]
int test_pack_union_cap(void) {
    if (sizeof(union PackUnion1173) != 4)
        return 1;
    if (_Alignof(union PackUnion1173) != 2)
        return 2;
    return 42;
}

#pragma pack(2)
struct PackBitfield1173 {
    char a;
    int  b : 5;
    int  c;
};
#pragma pack()

[[cccc::test(return = 42)]]
int test_pack_bitfield_cap(void) {
    if (sizeof(struct PackBitfield1173) != 6)
        return 1;
    if (_Alignof(struct PackBitfield1173) != 2)
        return 2;
    if (offsetof(struct PackBitfield1173, c) != 2)
        return 3;
    return 42;
}
