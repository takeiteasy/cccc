// CCCC_FLAGS: --testing --std=c89 -Wpedantic
// Consolidated suite: C89 with pedantic warnings — C99 extensions warn but compile
// Source tests: test_pedantic_long_long_warning, test_std_c89_compound_literal_error,
//               test_std_c89_designated_init_error, test_std_c89_line_comment_error,
//               test_std_c89_mixed_decl_error, test_std_c89_vla_error
//
// Note: compile-error tests (test_std_c89_{bool,flexible_array,inline,restrict,
//       stdbool_include}_error) remain as legacy tests — their errors fire at
//       tokenise/preprocess time and cannot be caught per-function in the suite
//       framework. test_pedantic_{compound,line}_error (--std=c89 -Werror=pedantic)
//       also remain as legacy since they need -Werror=pedantic at file scope.

#pragma cccc suite begin "std_c89"

// test_pedantic_long_long_warning: 'long long' warns in pedantic C89 but is allowed
[[cccc::test(return = 42)]]
int test_pedantic_long_long(void) {
    long long x = 1;
    return (int)x == 1 ? 42 : 1;
}

// test_std_c89_compound_literal_error: compound literals warn in pedantic C89
[[cccc::test(return = 42)]]
int test_std_c89_compound_literal(void) {
    int *p = (int []){1, 2, 3};
    return p[0] == 1 ? 42 : 1;
}

// test_std_c89_designated_init_error: designated initializers warn in pedantic C89
[[cccc::test(return = 42)]]
int test_std_c89_designated_init(void) {
    int arr[3] = { [1] = 42 };
    return arr[1];
}

// test_std_c89_line_comment_error: '//' comments warn in pedantic C89
[[cccc::test(return = 42)]]
int test_std_c89_line_comment(void) {
    int x = 1; // this is a line comment
    return x == 1 ? 42 : 1;
}

// test_std_c89_mixed_decl_error: mixed declarations warn in pedantic C89
[[cccc::test(return = 42)]]
int test_std_c89_mixed_decl(void) {
    int x = 1;
    x = 2;
    int y = 3;
    return x + y == 5 ? 42 : 1;
}

// test_std_c89_vla_error: VLAs warn in pedantic C89 but compile
[[cccc::test(return = 42)]]
int test_std_c89_vla(void) {
    int n = 1;
    int arr[n];
    arr[0] = 42;
    return arr[0];
}

#pragma cccc suite end
