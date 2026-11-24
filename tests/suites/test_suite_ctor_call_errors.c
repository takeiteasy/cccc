// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: T(args) without a `new` method, or on a non-struct, is rejected.

typedef struct {
    int x;
} Plain;

@template(T) struct Box {
    T v;
};

typedef int Num;

[[cccc::test(error = "'Plain' has no constructor; add @ctor")]]
void no_ctor_on_typedef(void) {
    Plain p = Plain(1);
}

[[cccc::test(error = "'Box' has no constructor; add @ctor")]]
void no_ctor_on_template_instance(void) {
    Box<int> b = Box<int>(1);
}

[[cccc::test(error = "'Num' is not a struct or union; it has no constructor")]]
void non_struct_typedef(void) {
    int n = Num(3);
}
