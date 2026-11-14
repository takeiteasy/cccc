// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: a static method is called through its type.
#pragma cccc suite begin "methods_static_errors"

// clang-format off
typedef struct Vec2 {
    int x, y;
    static Vec2 make(int x, int y) {
        return (Vec2){x, y};
    }
} Vec2;

[[cccc::test(error = "'make' is a static method; call it as T.make(...)")]]
void test_static_method_on_a_value(void) {
    Vec2 v = {1, 2};
    v.make(1, 2);
}

[[cccc::test(error = "'make' is a static method; call it as T.make(...)")]]
void test_static_method_on_a_pointer(void) {
    Vec2 v = {1, 2};
    Vec2 *p = &v;
    p->make(1, 2);
}

[[cccc::test(error = "no method 'nope' for this type")]]
void test_no_such_static_method(void) {
    Vec2.nope(1);
}

[[cccc::test(error = "'make' is a method; call it as T.make(...)")]]
void test_static_method_without_a_call(void) {
    auto f = Vec2.make;
}

[[cccc::test(error = "'.' after a type needs a struct or union")]]
void test_type_that_is_not_a_struct(void) {
    typedef int I;
    I.f();
}
// clang-format on

#pragma cccc suite end
