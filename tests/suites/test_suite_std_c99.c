// CCCC_FLAGS: --testing --std=c99
// Consolidated suite: C99 features valid in --std=c99 mode
// Source tests: test_has_feature_std_mode, test_std_c99_compound_literal_ok,
//               test_std_c99_designated_init_ok,
//               test_std_c99_flexible_array_ok, test_std_c99_inline_ok,
//               test_std_c99_line_comment_ok, test_std_c99_mixed_decl_ok,
//               test_std_c99_restrict_ok, test_std_c99_vla_ok
// Migrated (#612): test_pedantic_cccc_macro_silent (CCCC_REJECT_STDERR not
// preserved),
//   test_std_c99_anon_struct_error, test_std_c99_generic_error
//   (CCCC_EXPECT_STDERR not preserved — suite framework has no per-test stderr
//   matching)
//
// Deferred (compile-error tests, tokenise/preprocess-time errors cannot be
// caught per-function): test_std_c99_static_assert_error,
// test_std_c99_stdalign_include_error Kept legacy (non-recoverable warn→error
// via error_tok, cannot be per-function):
//   test_pedantic_compound_literal_error, test_pedantic_line_comment_error

// File-scope helper used by test_std_c99_inline (inline is a file-scope
// specifier)
static inline int c99_add(int a, int b) {
    return a + b;
}

// File-scope comptime helper for test_pedantic_cccc_macro_silent (#612).
// [[cccc::comptime]] must be defined at file scope; the test verifies it does
// not generate a [-Wpedantic] warning when compiled with --std=c99 -Wpedantic.
[[cccc::comptime]]
Node *c99_get_42(void) {
    return MakeIntLiteral(42);
}

// File-scope _Generic macro for test_std_c99_generic (#612).
#define c99_abs(x) _Generic((x), int: ((x) < 0 ? -(x) : (x)))

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
    int *p = (int[]){10, 20, 30};
    return p[1];
}

// test_std_c99_designated_init_ok
[[cccc::test(return = 42)]]
int test_std_c99_designated_init(void) {
    int arr[3] = {[1] = 42};
    struct {
        int x;
        int y;
    } p = {.y = 10, .x = 5};
    return arr[1] == 42 && p.x == 5 ? 42 : 1;
}

// test_std_c99_flexible_array_ok
[[cccc::test(return = 42)]]
int test_std_c99_flexible_array(void) {
    struct S {
        int len;
        int data[];
    };
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
    x     = 2;
    int y = 3;
    return x + y == 5 ? 42 : 1;
}

// test_std_c99_restrict_ok: restrict qualifier accepted in C99
[[cccc::test(return = 42)]]
int test_std_c99_restrict(void) {
    // restrict is valid in C99; just verify the declaration compiles
    int a[2] = {1, 2}, b[2] = {3, 4};
    int *restrict pa = a;
    int *restrict pb = b;
    return pa[0] + pb[1] == 5 ? 42 : 1;
}

// test_std_c99_vla_ok
[[cccc::test(return = 42)]]
int test_std_c99_vla(void) {
    int n = 3;
    int arr[n];
    for (int i = 0; i < n; i++)
        arr[i] = i * 10;
    return arr[2] == 20 ? 42 : 1;
}

// test_pedantic_cccc_macro_silent (#612): [[cccc::comptime]] does not generate
// a [-Wpedantic] warning in C99 pedantic mode. CCCC_REJECT_STDERR is not
// preserved here — suite tests cannot check per-test stderr output.
[[cccc::test(return = 42, flags = "-Wpedantic")]]
int test_pedantic_cccc_macro_silent(void) {
    return c99_get_42();
}

// test_std_c99_anon_struct (#612): anonymous structs are a C11 extension;
// they should warn (not error) in --std=c99 -Wpedantic. Code returns 42.
// CCCC_EXPECT_STDERR for the pedantic warning is not preserved in suite format.
[[cccc::test(return = 42, flags = "-Wpedantic")]]
int test_std_c99_anon_struct(void) {
    struct {
        int x;
        struct {
            int a;
            int b;
        };
    } o;
    o.a = 40;
    o.b = 2;
    return o.a + o.b;
}

// test_std_c99_generic (#612): _Generic is a C11 extension; warns (not errors)
// in --std=c99 -Wpedantic. CCCC_EXPECT_STDERR not preserved in suite format.
[[cccc::test(return = 42, flags = "-Wpedantic")]]
int test_std_c99_generic(void) {
    return c99_abs(-5) == 5 ? 42 : 1;
}

#pragma cccc suite end
