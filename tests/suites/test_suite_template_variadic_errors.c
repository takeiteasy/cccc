// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: misuse of a template parameter pack is rejected.

@template(T, Ts...) struct Head {
    T x;
};
@template(Ts...) int bare(Ts... xs) {
    Ts y;
    return 0;
}
@template(Ts...) int out_of_range(Ts... xs) {
    return xs...[2];
}
@template(Ts...) int no_pattern(Ts... xs) {
    int a = 1;
    return g(xs, a...);
}
@template(Ts...) int empty_fold(Ts... xs) {
    return (xs + ...);
}
@template(Ts...) int nested(Ts... xs) {
    return sum2(sum2(xs...)...);
}
@template(Ts...) int nested_fold(Ts... xs) {
    return sum2((xs + ... + 0)...);
}
@template(Ts...) int two_per_item(Ts... xs) {
    return h(xs... + xs...);
}
@template(unsigned char... Bs) int bytes(void) {
    return (Bs + ... + 0);
}
@template(int... Ns) int ints(void) {
    return (Ns + ... + 0);
}
@template(Ts...) int both_sides(Ts... xs) {
    return (xs + ... + xs);
}
@template(Ts...) int bad_index(Ts... xs) {
    return xs...[1.5];
}

@template(T, U, V = T, Ts...) struct Needs2 {
    T x;
};

#pragma cccc suite begin "template_variadic_errors"

[[cccc::test(error = "takes at least 1 argument, 0 given")]]
void test_too_few_arguments(void) {
    @inst Head h;
}

[[cccc::test(error = "parameter pack 'Ts' must be expanded with '...'")]]
void test_bare_pack_name(void) {
    bare @(int)(1);
}

[[cccc::test(error = "pack index 2 is out of range for 2 elements")]]
void test_index_out_of_range(void) {
    out_of_range @(int, int)(1, 2);
}

[[cccc::test(error = "a pack index must be an integer constant")]]
void test_index_not_integer(void) {
    bad_index @(int, int)(1, 2);
}

[[cccc::test(error = "the pattern before '...' contains no parameter pack")]]
void test_pattern_without_a_pack(void) {
    no_pattern @(int)(1);
}

[[cccc::test(error = "of an empty pack needs an initial value")]]
void test_empty_fold_without_an_identity(void) {
    empty_fold @()();
}

[[cccc::test(error = "the pattern before '...' contains no parameter pack")]]
void test_outer_pattern_without_a_free_pack(void) {
    nested @(int)(1);
}

[[cccc::test(error = "the pattern before '...' contains no parameter pack")]]
void test_outer_pattern_of_only_a_fold(void) {
    nested_fold @(int)(1);
}

[[cccc::test(error = "a list item can hold only one pack expansion")]]
void test_two_expansions_in_one_item(void) {
    two_per_item @(int)(1);
}

[[cccc::test(error = "only one side of a binary fold")]]
void test_pack_on_both_sides_of_a_fold(void) {
    both_sides @(int)(1);
}

[[cccc::test(error = "template argument 300 does not fit 'unsigned char'")]]
void test_constant_pack_element_must_fit(void) {
    bytes @(1, 300)();
}

[[cccc::test(error = "expected an expression, found 'int'")]]
void test_constant_pack_takes_constants(void) {
    ints @(int)();
}

@template(double... Ds) double sum_d(void) {
    return (Ds + ... + 0.0);
}
@template(long double... Ls) int long_d(void) {
    return 0;
}
@template(int *...Ps) int ptrs(void) {
    return 0;
}

[[cccc::test(error = "NaN is not a valid template argument")]]
void test_float_pack_rejects_nan(void) {
    sum_d @(1.0, __builtin_nan(""))();
}

[[cccc::test(error = "cannot have type long double")]]
void test_long_double_pack_is_rejected(void) {
    long_d @(1.0L)();
}

[[cccc::test(error = "with file-scope or static storage")]]
void test_pointer_pack_needs_static_storage(void) {
    int local = 0;
    ptrs @(&local)();
}

@template(Ts...) struct Amb {
    int v;
};
@template(Ts...) @specialize(Ts *...) struct Amb {
    int v;
};
@template(Ts...) @specialize(int *, Ts...) struct Amb {
    int v;
};

[[cccc::test(error = "ambiguous partial specializations")]]
void test_element_pattern_ambiguity(void) {
    @inst(int *, char *) Amb a;
}

#pragma cccc suite end

[[cccc::test(error = "takes at least 2 arguments, 1 given")]]
void test_too_few_arguments_before_defaults(void) {
    @inst(int) Needs2 n;
}
