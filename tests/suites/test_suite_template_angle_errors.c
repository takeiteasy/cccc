// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: malformed Name<T> is rejected.

@template(T) struct Vec {
    T x;
};
@template(T) T ident(T a) {
    return a;
}
@template(A, B) struct Pair {
    A a;
    B b;
};

[[cccc::test(
    error = "expected '>' to close the template argument list for 'Vec'")]]
void unterminated(void) {
    Vec < int x;
}

[[cccc::test(error = "template 'ident' is a function, not a type")]]
void function_template_in_type_position(void) {
    ident<int> x;
}

[[cccc::test(error = "template 'Vec' is a type, not a function")]]
void type_template_called(void) {
    int x = Vec<int>;
}

[[cccc::test(error = "takes 2 arguments, 1 given")]]
void too_few_arguments(void) {
    Pair<int> p;
}

[[cccc::test(error = "takes 1 argument, 2 given")]]
void too_many_arguments(void) {
    Vec<int, int> v;
}
