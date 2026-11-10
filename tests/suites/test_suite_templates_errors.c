// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: malformed @template and @inst are rejected.

#include <stddef.h>

@template(T) struct Vec { T *data; size_t len; };
@template(T) T ident(T a) { return a; }
@template(A, B) struct Pair { A a; B b; };

#pragma cccc suite begin "templates_errors"

[[cccc::test(error = "takes 2 type arguments, 1 given")]]
void test_too_few_arguments(void) {
    @inst(Pair, int) p;
}

[[cccc::test(error = "takes 1 type argument, 2 given")]]
void test_too_many_arguments(void) {
    @inst(Vec, int, int) v;
}

[[cccc::test(error = "'Nope' is not a template")]]
void test_unknown_template(void) {
    @inst(Nope, int) v;
}

[[cccc::test(error = "template 'Vec' is a type, not a function")]]
void test_struct_template_as_expression(void) {
    int x = @inst(Vec, int);
}

[[cccc::test(error = "only allowed on a file-scope")]]
void test_template_inside_function(void) {
    @template(T) T inner(T a) { return a; }
}

[[cccc::test(error = "checked-pointer type cannot be a template argument")]]
void test_checked_pointer_argument(void) {
    int * [[cccc::single]] p = 0;
    @inst(Vec, typeof(p)) v;
}

[[cccc::test(error = "variable-length array cannot be a template argument")]]
void test_vla_argument(void) {
    int n = 3;
    @inst(Vec, int[n]) v;
}

#pragma cccc suite end
