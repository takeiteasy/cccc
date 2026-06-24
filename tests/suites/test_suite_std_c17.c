// CCCC_FLAGS: --testing --std=c17
// Consolidated suite: C17 features
// Source tests: test_c17_empty_params_variadic, test_c17_label_decl_braces_ok,
//               test_c17_label_decl_null_stmt_ok, test_c17_recursive_main,
//               test_c23_keywords_pre_c23_idents, test_std_c17_constexpr_ok,
//               test_std_c17_attributes_error
//
// Migrated (#612): test_std_c17_binary_literal_error (CCCC_EXPECT_STDERR not
//   preserved — suite framework has no per-test stderr matching)
//
// Deferred (whole-file compile errors, not per-function catchable):
//   test_c23_label_before_decl_error, test_c23_compound_literal_storage_c17_error,
//   test_std_c17_digit_separator_error, test_std_c17_embed_error

// C17: int foo() with no prototype — K&R style; must be defined at file scope
int c17_kr_add();
int c17_kr_add(int a, int b) { return a + b; }

// C17 keywords downgraded to plain identifiers (pre-C23)
int c17_bool = 1;
int c17_true = 2;
int c17_false = 3;
int c17_nullptr = 4;
int c17_constexpr = 42;

static int c17_depth = 0;
int c17_recursive_fn();
int c17_recursive_fn() {
    if (c17_depth++ == 0)
        c17_recursive_fn(1, "arg");
    return c17_depth == 2 ? 42 : 1;
}

[[nodiscard]] int c17_nodiscard_fn(void) { return 42; }

#pragma cccc suite begin "std_c17"

// test_c17_empty_params_variadic: K&R-style empty params accept any args in C17
[[cccc::test(return = 42)]]
int test_c17_empty_params_variadic(void) {
    return c17_kr_add(20, 22) == 42 ? 42 : 1;
}

// test_c17_label_decl_braces_ok: declaration after label in braces is OK in C17
[[cccc::test(return = 42)]]
int test_c17_label_decl_braces_ok(void) {
    int v = 1;
    switch (v) {
        case 1: { int x = 5; return x == 5 ? 42 : 1; }
    }
    return 1;
}

// test_c17_label_decl_null_stmt_ok: declaration after label + null stmt is OK
[[cccc::test(return = 42)]]
int test_c17_label_decl_null_stmt_ok(void) {
    int v = 1;
    switch (v) {
        case 1:;
            int x = 5;
            return x == 5 ? 42 : 1;
    }
    return 1;
}

// test_c17_recursive_main: recursive fn() with extra args is legal in C17 (no prototype)
[[cccc::test(return = 42)]]
int test_c17_recursive_main(void) {
    c17_depth = 0;
    return c17_recursive_fn();
}

// test_c23_keywords_pre_c23_idents: bool/true/false/nullptr usable as identifiers in C17
[[cccc::test(return = 42)]]
int test_c23_keywords_pre_c23_idents(void) {
    int sum = c17_bool + c17_true + c17_false + c17_nullptr;
    return sum == 10 ? 42 : 1;
}

// test_std_c17_constexpr_ok: constexpr is a plain identifier in C17
[[cccc::test(return = 42)]]
int test_std_c17_constexpr_ok(void) {
    return c17_constexpr == 42 ? 42 : 1;
}

// test_std_c17_attributes_error: [[nodiscard]] ignored on return type in C17
[[cccc::test(return = 42)]]
int test_std_c17_attributes(void) {
    return c17_nodiscard_fn();
}

// test_std_c17_binary_literal (#612): binary literals are a C23 extension;
// should warn (not error) in --std=c17 -Wpedantic. Code returns 42.
// CCCC_EXPECT_STDERR for the pedantic warning is not preserved in suite format.
[[cccc::test(return = 42, flags = "-Wpedantic")]]
int test_std_c17_binary_literal(void) {
    return 0b101010 == 42 ? 42 : 1;
}

#pragma cccc suite end
