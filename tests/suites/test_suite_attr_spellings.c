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

// Template markers: @template, [[template]], [[ns::template]], __template.
[[cccc::template(T)]] struct BoxCccc {
    T v;
};
[[template(T)]] T add1(T a) {
    return a + 1;
}
[[gnu::template(T)]] T add2(T a) {
    return a + 2;
}
__attribute__((template(T, int N = 8))) struct Arr {
    T v[N];
};
__template(T) T add3(T a) {
    return a + 3;
}
__template__(T) T add4(T a) {
    return a + 4;
}
@template(T) T add5(T a) {
    return a + 5;
}
[[cccc::specialize(int)]] int add1(int a) {
    return a + 100;
}
__specialize(long) long add3(long a) {
    return a + 300;
}
@specialize(long) long add5(long a) {
    return a + 500;
}

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

[[cccc::test]]
void test_template_marker_spellings(void) {
    __inst(int) BoxCccc b = {7};
    AssertEq(b.v, 7);
    AssertEq(__inst(char) add1(1), 2);
    AssertEq(__inst(int) add1(1), 101);
    AssertEq(__inst(int) add2(1), 3);
    AssertEq(__inst(int) add3(1), 4);
    AssertEq(__inst(long) add3(1L), 301);
    AssertEq(__inst(int) add4(1), 5);
    AssertEq(@inst(int) add5(1), 6);
    AssertEq(@inst(long) add5(1L), 501);
    AssertEq(__inst__(int) add5(1), 6);
}

[[cccc::test]]
void test_template_marker_defaults(void) {
    __inst(int) Arr    a;
    __inst(int, 4) Arr c;
    AssertEq(sizeof a, 32);
    AssertEq(sizeof c, 16);
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
    AssertTrue(__has_c_attribute(cccc::aligned));
    AssertTrue(__has_c_attribute(clang::aligned));
    AssertTrue(__has_c_attribute(aligned));
    AssertTrue(__has_c_attribute(gnu::single));
    AssertFalse(__has_c_attribute(cccc::no_such_attribute));
}

#pragma cccc suite end
