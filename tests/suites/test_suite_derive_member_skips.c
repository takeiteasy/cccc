// CCCC_FLAGS: --testing
// Suite: @serialize_skip, @accessor_skip and @constructor_skip, run on the VM
// and under -c=native.

#include <cccc/derive.h>
#include <string.h>

@serialize @deserialize struct S {
    int keep;
    int secret @serialize_skip;
    int tail;
};

@generate_accessors struct A {
    int shown;
    int hidden @accessor_skip;
};

@generate_constructor struct C {
    int given;
    int unset @constructor_skip;
    int also;
};

[[cccc::test]]
void test_serialize_skip(void) {
    struct S s = {1, 99, 3};
    char     buf[sizeof(struct S)];
    memset(buf, 0, sizeof buf);
    AssertEq(S_serialize(&s, buf), (int)sizeof(struct S));
    struct S r = S_deserialize(buf);
    AssertEq(r.keep, 1);
    AssertEq(r.secret, 0);
    AssertEq(r.tail, 3);
}

[[cccc::test]]
void test_accessor_skip(void) {
    struct A a = {5, 6};
    set_shown(&a, 7);
    AssertEq(get_shown(&a), 7);
    AssertEq(a.hidden, 6);
}

[[cccc::test]]
void test_constructor_skip(void) {
    struct C c = C_create(4, 8);
    AssertEq(c.given, 4);
    AssertEq(c.unset, 0);
    AssertEq(c.also, 8);
}
