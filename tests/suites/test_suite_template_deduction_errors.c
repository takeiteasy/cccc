// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: a call that cannot deduce its template arguments says why.
#pragma cccc suite begin "template_deduction_errors"

@template(T) struct Vec {
    T x, y;
};

@template(T) T maxof(T a, T b) {
    return a > b ? a : b;
}
@template(T, U) U cast_to(T a) {
    return (U)a;
}
@template(T, int N) int len_of(T a[N]) {
    return N;
}
@template(T) int deref(T *p) {
    return *p;
}
@template(T) T broken(T a) {
    return a + missing_name;
}
@template(Ts...) int count_of(Ts... xs) {
    return sizeof...(Ts);
}
@template(T) int vec_len(@inst(T) Vec v) {
    return 2;
}

[[cccc::test(error = "cannot deduce 'T' for 'maxof': argument 2 is 'double', "
                     "but 'int' was deduced before")]]
void test_conflicting_arguments(void) {
    double d = maxof(1, 2.0);
}

[[cccc::test(error = "template parameter 'U' is not deduced by the arguments")]]
void test_parameter_only_in_return_type(void) {
    int x = cast_to(1);
}

[[cccc::test(error = "template parameter 'N' is not deduced by the arguments")]]
void test_array_parameter_has_no_length(void) {
    int a[3] = {1, 2, 3};
    int n    = len_of(a);
}

[[cccc::test(error = "argument 1 ('int') does not match its parameter")]]
void test_argument_does_not_fit_the_pattern(void) {
    deref(5);
}

[[cccc::test(error = "argument 1 ('int') does not match its parameter")]]
void test_scalar_does_not_fit_instance_pattern(void) {
    vec_len(5);
}

[[cccc::test(
    error = "cannot deduce the arguments of variadic template 'count_of'")]]
void test_pack_template_is_not_deduced(void) {
    count_of(1, 2);
}

[[cccc::test(error = "takes positional arguments")]]
void test_keyed_argument(void) {
    int x = maxof(1, .b = 2);
}

[[cccc::test(error = "too few arguments")]]
void test_too_few_arguments(void) {
    int x = maxof(1);
}

[[cccc::test(error = "too many arguments")]]
void test_too_many_arguments(void) {
    int x = maxof(1, 2, 3);
}

[[cccc::test(error = "undefined variable 'missing_name'")]]
void test_error_in_the_deduced_instance_is_reported(void) {
    int x = broken(1);
}

#pragma cccc suite end
