// CCCC_FLAGS: --testing
// Suite: one attribute, every spelling: [[x]], [[cccc::x]], [[gnu::x]],
// [[__gnu__::x]], __attribute__((x)), __attribute__((__x__)), @x.

#include <stdint.h>

// Layout: observable through _Alignof / sizeof.
struct [[gnu::aligned(16)]] AlignGnu {
    char c;
};
struct [[__gnu__::aligned(16)]] AlignGnuU {
    char c;
};
struct __attribute__((aligned(16))) AlignAttr {
    char c;
};
struct @aligned(16) AlignAt {
    char c;
};
struct [[aligned(16)]] AlignStd {
    char c;
};
struct [[cccc::aligned(16)]] AlignCccc {
    char c;
};
struct [[__aligned__(16)]] AlignStdU {
    char c;
};
struct __attribute__((__aligned__(16))) AlignAttrU {
    char c;
};

struct [[gnu::packed]] PackGnu {
    char c;
    int  i;
};
struct [[__gnu__::packed]] PackGnuU {
    char c;
    int  i;
};
struct __attribute__((packed)) PackAttr {
    char c;
    int  i;
};
struct @packed PackAt {
    char c;
    int  i;
};
struct [[packed]] PackStd {
    char c;
    int  i;
};
struct [[cccc::packed]] PackCccc {
    char c;
    int  i;
};
struct __attribute__((__packed__)) PackAttrU {
    char c;
    int  i;
};

// Behaviour: cleanup runs code.
static int cleaned;
static void note_clean(int *p) {
    cleaned += *p;
}

#pragma cccc suite begin "attr_spellings"

[[cccc::test]]
void test_aligned_spellings(void) {
    AssertEq(_Alignof(struct AlignGnu), 16);
    AssertEq(_Alignof(struct AlignGnuU), 16);
    AssertEq(_Alignof(struct AlignAttr), 16);
    AssertEq(_Alignof(struct AlignAt), 16);
    AssertEq(_Alignof(struct AlignStd), 16);
    AssertEq(_Alignof(struct AlignCccc), 16);
    AssertEq(_Alignof(struct AlignStdU), 16);
    AssertEq(_Alignof(struct AlignAttrU), 16);
}

[[cccc::test]]
void test_packed_spellings(void) {
    AssertEq(sizeof(struct PackGnu), 5);
    AssertEq(sizeof(struct PackGnuU), 5);
    AssertEq(sizeof(struct PackAttr), 5);
    AssertEq(sizeof(struct PackAt), 5);
    AssertEq(sizeof(struct PackStd), 5);
    AssertEq(sizeof(struct PackCccc), 5);
    AssertEq(sizeof(struct PackAttrU), 5);
}

[[cccc::test]]
void test_cleanup_spellings(void) {
    cleaned = 0;
    {
        [[gnu::cleanup(note_clean)]] int         a = 1;
        __attribute__((cleanup(note_clean))) int b = 2;
        @cleanup(note_clean) int                 c = 4;
        (void)a;
        (void)b;
        (void)c;
    }
    AssertEq(cleaned, 7);
}

// Introspection: every spelling a registered attribute has reports it.
[[cccc::test]]
void test_has_attribute_spellings(void) {
    AssertTrue(__has_attribute(aligned));
    AssertTrue(__has_attribute(__aligned__));
    AssertTrue(__has_attribute(single));
    AssertTrue(__has_c_attribute(gnu::aligned));
    AssertTrue(__has_c_attribute(__gnu__::aligned));
    AssertTrue(__has_c_attribute(cccc::single));
    AssertTrue(__has_c_attribute(deprecated));
    AssertFalse(__has_c_attribute(cccc::no_such_attribute));
}

// TODO(#2008): rejected or ignored today, enabled as the attribute dispatch
// resolves by name alone:
//   [[unknown_vendor::x]] is a parse error, not a warning

#pragma cccc suite end
