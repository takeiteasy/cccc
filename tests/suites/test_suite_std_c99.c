// CCCC_FLAGS: --testing --std=c99
// Consolidated suite: C99 features valid in --std=c99 mode
// Source tests: test_has_feature_std_mode, test_std_c99_compound_literal_ok,
//               test_std_c99_designated_init_ok, test_std_c99_flexible_array_ok,
//               test_std_c99_inline_ok, test_std_c99_line_comment_ok,
//               test_std_c99_mixed_decl_ok, test_std_c99_restrict_ok,
//               test_std_c99_vla_ok
//
// Deferred (compile-error tests, tokenise/preprocess-time errors cannot be caught
// per-function): test_std_c99_static_assert_error, test_std_c99_stdalign_include_error
// Deferred (--std=c99 -Wpedantic): test_pedantic_cccc_macro_silent,
//   test_std_c99_anon_struct_error, test_std_c99_generic_error

// File-scope helper used by test_std_c99_inline (inline is a file-scope specifier)
static inline int c99_add(int a, int b) { return a + b; }

#pragma cccc suite begin "std_c99"

// test_has_feature_std_mode
[[cccc::test(return = 42)]]
int test_has_feature_std_mode(void) {
#if !__has_feature(c99)
    return 1;
#endif
#if __has_feature(c11)
    return 2;
#endif
#if __has_feature(c23)
    return 3;
#endif
    return 42;
}

// test_std_c99_compound_literal_ok
[[cccc::test(return = 20)]]
int test_std_c99_compound_literal(void) {
    int *p = (int []){10, 20, 30};
    return p[1];
}

// test_std_c99_designated_init_ok
[[cccc::test(return = 42)]]
int test_std_c99_designated_init(void) {
    int arr[3] = { [1] = 42 };
    struct { int x; int y; } p = { .y = 10, .x = 5 };
    return arr[1] == 42 && p.x == 5 ? 42 : 1;
}

// test_std_c99_flexible_array_ok
[[cccc::test(return = 42)]]
int test_std_c99_flexible_array(void) {
    struct S { int len; int data[]; };
    return 42;
}

// test_std_c99_inline_ok
[[cccc::test(return = 42)]]
int test_std_c99_inline(void) {
    return c99_add(1, 2) == 3 ? 42 : 1;
}

// test_std_c99_line_comment_ok
[[cccc::test(return = 42)]]
int test_std_c99_line_comment(void) {
    int x = 1; // line comments are valid in C99
    return 42;
}

// test_std_c99_mixed_decl_ok
[[cccc::test(return = 42)]]
int test_std_c99_mixed_decl(void) {
    int x = 1;
    x = 2;
    int y = 3;
    return x + y == 5 ? 42 : 1;
}

// test_std_c99_restrict_ok: restrict qualifier accepted in C99
[[cccc::test(return = 42)]]
int test_std_c99_restrict(void) {
    // restrict is valid in C99; just verify the declaration compiles
    int a[2] = {1, 2}, b[2] = {3, 4};
    int * restrict pa = a;
    int * restrict pb = b;
    return pa[0] + pb[1] == 5 ? 42 : 1;
}

// test_std_c99_vla_ok
[[cccc::test(return = 42)]]
int test_std_c99_vla(void) {
    int n = 3;
    int arr[n];
    for (int i = 0; i < n; i++) arr[i] = i * 10;
    return arr[2] == 20 ? 42 : 1;
}

#pragma cccc suite end
