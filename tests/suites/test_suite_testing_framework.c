// CCCC_FLAGS: --testing
// Consolidated suite: [[cccc::test]] framework self-tests.
// Source tests: test_framework, test_error_count_ops, test_exit_code,
//   test_include_route_test, test_return_assertion, test_return_epsilon,
//   test_return_struct, test_return_types.

#include <stdlib.h>

// [from test_framework.c]
// Self-test for the CCCC built-in testing framework.

[[cccc::test]]
void test_assert_true(void) {
    Assert(1 == 1);
    Assert(42 != 0);
}

[[cccc::test]]
void test_assert_eq(void) {
    AssertEq(1 + 1, 2);
    AssertEq(6 * 7, 42);
}

[[cccc::test]]
void test_assert_neq(void) {
    AssertNeq(1, 2);
    AssertNeq(0, 42);
}

[[cccc::test]]
void test_assert_null(void) {
    void *p = 0;
    AssertNull(p);
}

[[cccc::test]]
void test_assert_not_null(void) {
    int x = 42;
    AssertNotNull(&x);
}

[[cccc::test]]
void test_assert_streq(void) {
    AssertStrEq("hello", "hello");
    AssertStrEq("", "");
}

[[cccc::test(suite = "framework/math")]]
void test_addition(void) {
    AssertEq(1 + 1, 2);
    AssertEq(10 + 32, 42);
}

[[cccc::test(suite = "framework/math")]]
void test_subtraction(void) {
    AssertEq(5 - 3, 2);
    AssertEq(100 - 58, 42);
}

#pragma cccc suite begin "framework/strings"

[[cccc::test]]
void test_string_equality(void) {
    AssertStrEq("foo", "foo");
}

[[cccc::test]]
void test_string_empty(void) {
    AssertStrEq("", "");
}

#pragma cccc suite end

static int fw_multiply(int a, int b) {
    return a * b;
}

[[cccc::test]]
void test_calls_helper(void) {
    AssertEq(fw_multiply(3, 7), 21);
    AssertEq(fw_multiply(0, 100), 0);
    AssertEq(fw_multiply(-2, 5), -10);
}

static int fw_square(int x);

[[cccc::test]]
void test_calls_forward(void) {
    AssertEq(fw_square(5), 25);
    AssertEq(fw_square(0), 0);
}

static int fw_square(int x) {
    return x * x;
}

[[cccc::test]]
void test_local_struct(void) {
    struct {
        int x;
        int y;
    } pt;
    pt.x = 3;
    pt.y = 4;
    AssertEq(pt.x + pt.y, 7);
    AssertNotNull(&pt);
}

[[cccc::test]]
void test_multiple_assertions(void) {
    AssertEq(1 + 1, 2);
    AssertNeq(1, 2);
    AssertStrEq("hello", "hello");
    AssertNull((void *)0);
    int x = 42;
    AssertNotNull(&x);
}

#pragma cccc suite begin "framework/negative"

[[cccc::test(error = "undefined variable")]]
void test_neg_undeclared(void) {
    int x = totally_undeclared_variable;
}

[[cccc::test(error = "undefined variable")]]
void test_neg_undeclared_in_expr(void) {
    int arr[3];
    int x = arr[totally_undefined_index];
}

[[cccc::test(error = "undefined variable")]]
void test_neg_suite_and_error(void) {
    int y = also_does_not_exist;
}

#pragma cccc suite end

[[cccc::test(name = "addition is commutative")]]
void test_addition_commutative(void) {
    AssertEq(1 + 2, 2 + 1);
    AssertEq(10 + 5, 5 + 10);
}

[[cccc::test(name = "named with suite", suite = "framework/display_name")]]
void test_named_with_suite(void) {
    Assert(1 == 1);
}

static int g_counter = 7;

[[cccc::test]]
void test_global_state_reset_a(void) {
    g_counter = 42;
    AssertEq(g_counter, 42);
}

[[cccc::test]]
void test_global_state_reset_b(void) {
    AssertEq(g_counter, 7);
}

static int g_setup_count = 0;

[[cccc::test_setup]]
void fw_global_setup(void) {
    g_setup_count++;
}

#pragma cccc suite begin "framework/setup_teardown"

[[cccc::test]]
void test_global_setup_ran(void) {
    AssertEq(g_setup_count, 1);
}

[[cccc::test]]
void test_global_setup_runs_per_test(void) {
    AssertEq(g_setup_count, 1);
}

#pragma cccc suite end

static int g_teardown_marker = 0;

[[cccc::test_teardown]]
void fw_global_teardown(void) {
    g_teardown_marker = 99;
}

[[cccc::test]]
void test_teardown_doesnt_crash(void) {
    Assert(1 == 1);
}

static int g_pattern_ran = 0;

[[cccc::test_setup(name = "fw_pattern_*")]]
void fw_pattern_setup(void) {
    g_pattern_ran = 1;
}

[[cccc::test]]
void test_no_pattern_match(void) {
    AssertEq(g_pattern_ran, 0);
}

[[cccc::test(name = "fw_pattern_match")]]
void test_fw_pattern_match_fn(void) {
    AssertEq(g_pattern_ran, 1);
}

static int g_once_setup_count  = 0;
static int g_once_teardown_ran = 0;

[[cccc::test_setup(suite = "framework/once_suite", once)]]
void fw_once_suite_setup(void) {
    g_once_setup_count++;
}

[[cccc::test_teardown(suite = "framework/once_suite", once)]]
void fw_once_suite_teardown(void) {
    g_once_teardown_ran = 1;
}

#pragma cccc suite begin "framework/once_suite"

[[cccc::test]]
void test_once_setup_ran(void) {
    AssertEq(g_once_setup_count, 1);
}

[[cccc::test]]
void test_once_setup_not_repeated(void) {
    AssertEq(g_once_setup_count, 1);
}

#pragma cccc suite end

#pragma cccc suite begin "framework/new_assertions"

[[cccc::test]]
void test_assert_false_works(void) {
    AssertFalse(0);
    AssertFalse(1 == 2);
}

[[cccc::test]]
void test_assert_gt_works(void) {
    AssertGt(10, 5);
    AssertGt(0, -1);
}

[[cccc::test]]
void test_assert_lt_works(void) {
    AssertLt(5, 10);
    AssertLt(-1, 0);
}

[[cccc::test]]
void test_assert_ge_works(void) {
    AssertGe(10, 10);
    AssertGe(10, 5);
}

[[cccc::test]]
void test_assert_le_works(void) {
    AssertLe(5, 5);
    AssertLe(5, 10);
}

[[cccc::test]]
void test_assert_within_works(void) {
    AssertWithin(2, 10, 9);
    AssertWithin(0, 42, 42);
}

[[cccc::test]]
void test_assert_streq_len_works(void) {
    AssertStrEqLen("hello", "hello world", 5);
    AssertStrEqLen("", "", 0);
}

[[cccc::test]]
void test_assert_true_false(void) {
    AssertTrue(1 == 1);
    AssertFalse(1 == 2);
}

[[cccc::test]]
void test_assert_true_msg_works(void) {
    AssertTrueMsg(1 == 1, "trivial truth");
}

[[cccc::test]]
void test_assert_eq_msg_works(void) {
    AssertEqMsg(42, 42, "the answer");
}

[[cccc::test]]
void test_assert_streq_msg_works(void) {
    AssertStrEqMsg("hello", "hello", "greeting");
}

#pragma cccc suite end

[[cccc::test(timeout = 5000)]]
void test_per_test_timeout(void) {
    AssertEq(1 + 1, 2);
}

#pragma cccc suite begin "framework/error_count"

[[cccc::test(error = "undefined variable", error_count = 1)]]
void test_neg_one_error(void) {
    int x = totally_not_defined;
}

#pragma cccc suite end

static int g_once_namepat_count = 0;

[[cccc::test_setup(name = "fw_once_namepat_*", once)]]
void fw_once_namepat_setup(void) {
    g_once_namepat_count++;
}

[[cccc::test]]
void test_once_namepat_not_matched(void) {
    AssertEq(g_once_namepat_count, 0);
}

[[cccc::test(name = "fw_once_namepat_first")]]
void test_once_namepat_matched_first(void) {
    AssertEq(g_once_namepat_count, 1);
}

[[cccc::test(name = "fw_once_namepat_second")]]
void test_once_namepat_matched_second(void) {
    AssertEq(g_once_namepat_count, 1);
}

#pragma cccc suite begin "framework/parent"

[[cccc::test(suite = "framework/parent/explicit")]]
void test_nested_attr_form(void) {
    AssertEq(1, 1);
}

[[cccc::test]]
void test_nested_parent_only(void) {
    AssertEq(2, 2);
}

#pragma cccc suite begin "child"

[[cccc::test]]
void test_nested_child(void) {
    AssertEq(3, 3);
}

#pragma cccc suite begin "grandchild"

[[cccc::test]]
void test_nested_grandchild(void) {
    AssertEq(4, 4);
}

#pragma cccc suite end

[[cccc::test]]
void test_nested_back_to_child(void) {
    AssertEq(5, 5);
}

#pragma cccc suite end

[[cccc::test]]
void test_nested_back_to_parent(void) {
    AssertEq(6, 6);
}

#pragma cccc suite end

[[cccc::test]]
void test_nested_no_suite(void) {
    AssertEq(7, 7);
}

// [from test_error_count_ops.c]
// error_count comparison operators (ticket #343).
#pragma cccc suite begin "framework/error_count_ops"

[[cccc::test(error = "undeclared", error_count = 1)]]
void test_ec_eq_one(void) {
    int a = undeclared_var;
}

[[cccc::test(error = "undeclared", error_count != 1)]]
void test_ec_ne_one(void) {
    int a = undeclared_x + undeclared_y;
}

[[cccc::test(error = "undeclared", error_count > 1)]]
void test_ec_gt_one(void) {
    int a = undeclared_x + undeclared_y;
}

[[cccc::test(error = "undeclared", error_count >= 2)]]
void test_ec_ge_two(void) {
    int a = undeclared_x + undeclared_y;
}

[[cccc::test(error = "undeclared", error_count < 3)]]
void test_ec_lt_three(void) {
    int a = undeclared_var;
}

[[cccc::test(error = "undeclared", error_count <= 2)]]
void test_ec_le_two(void) {
    int a = undeclared_x + undeclared_y;
}

#pragma cccc suite end

[[cccc::test(error != "nonexistent_pattern_xyz",
             suite = "framework/error_pat_negate")]]
void test_neg_pattern_absent(void) {
    int a = undeclared_var;
}

// [from test_exit_code.c]
#pragma cccc suite begin "framework/exit_code"

[[cccc::test(exit_code = 0)]]
int test_normal_exit(void) {
    return 0;
}

[[cccc::test(exit_code = 42)]]
void test_explicit_exit(void) {
    exit(42);
}

[[cccc::test(exit_code = 139)]]
int test_segfault(void) {
    volatile int *p = (volatile int *)0;
    return *p;
}

#pragma cccc suite end

// [from test_include_route_test.c]
// #include [[cccc::test]] routes the include only in testing mode.
#pragma cccc suite begin "framework/include_route"

#include[[cccc::test]] "../fixtures/test_only.h"

[[cccc::test]]
void test_include_loaded(void) {
#ifndef TEST_ONLY_LOADED
    AssertTrue(0);
#endif
    AssertTrue(1);
}

#pragma cccc suite end

// [from test_return_assertion.c]
// Return value assertions (ticket #342).
#pragma cccc suite begin "framework/return_assertion"

[[cccc::test(return = 0)]]
int test_return_zero(void) {
    return 0;
}

[[cccc::test(return = 1)]]
int test_return_one(void) {
    return 1;
}

[[cccc::test(return = 42)]]
int test_return_forty_two(void) {
    return 42;
}

[[cccc::test(return = -1)]]
int test_return_negative(void) {
    return -1;
}

[[cccc::test(return = 6)]]
int test_return_computed(void) {
    int x = 2;
    int y = 3;
    return x * y;
}

[[cccc::test(return = 7, name = "addition returns correct sum")]]
int test_return_named(void) {
    return 3 + 4;
}

#pragma cccc suite end

[[cccc::test(return = 100, suite = "framework/return_combined")]]
int test_return_with_suite(void) {
    return 100;
}

[[cccc::test(return = 1, suite = "framework/return_combined")]]
int test_return_with_assert(void) {
    AssertEq(1 + 1, 2);
    return 1;
}

// [from test_return_epsilon.c]
// Configurable float tolerance (ticket #351).
#pragma cccc suite begin "framework/return_epsilon"

[[cccc::test(return = 3.14159, return_epsilon = 1e-5)]]
double test_approx_pi(void) {
    return 3.141595;
}

[[cccc::test(return = 0.0, return_epsilon = 1e-3)]]
double test_zero_loose(void) {
    return 0.0005;
}

[[cccc::test(return != 99.0, return_epsilon = 0.5)]]
double test_ne_with_epsilon(void) {
    return 1.0;
}

[[cccc::test(return = 1.0)]]
double test_exact_one(void) {
    return 1.0;
}

#pragma cccc suite end

// [from test_return_struct.c]
// Struct return-value assertion with compound-literal syntax (ticket #353).
struct FwPoint {
    int x;
    int y;
};

#pragma cccc suite begin "framework/struct_return"

[[cccc::test(return = (struct FwPoint){.x = 1, .y = 2})]] struct FwPoint
    test_point_eq(void) {
    return (struct FwPoint){.x = 1, .y = 2};
}

[[cccc::test(return = (struct FwPoint){.x = 100, .y = 200})]] struct FwPoint
    test_point_large(void) {
    return (struct FwPoint){.x = 100, .y = 200};
}

[[cccc::test(return = (struct FwPoint){.x = -3, .y = -9})]] struct FwPoint
    test_point_negative(void) {
    return (struct FwPoint){.x = -3, .y = -9};
}

[[cccc::test(return = (struct FwPoint){.x = 7})]] struct FwPoint
    test_point_partial(void) {
    return (struct FwPoint){.x = 7, .y = 0};
}

[[cccc::test(return != (struct FwPoint){.x = 1, .y = 1})]] struct FwPoint
    test_point_ne(void) {
    return (struct FwPoint){.x = 99, .y = 2};
}

[[cccc::test(return = (struct FwPoint){.x = 42, .y = 0},
                    name = "point with name annotation")]] struct FwPoint
    test_point_named(void) {
    return (struct FwPoint){.x = 42, .y = 0};
}

#pragma cccc suite end

struct FwFPFields {
    float  a;
    double b;
};

[[cccc::test(return = (struct FwFPFields){.a = 1.5,
                                          .b = 3.14})]] struct FwFPFields
    test_fp_fields(void) {
    return (struct FwFPFields){.a = 1.5f, .b = 3.14};
}

struct FwMixed {
    int    code;
    double val;
};

[[cccc::test(return = (struct FwMixed){.code = 7,
                                       .val  = 2.718})]] struct FwMixed
    test_mixed_int_double(void) {
    return (struct FwMixed){.code = 7, .val = 2.718};
}

struct FwNamed {
    char *label;
    int   code;
};

[[cccc::test(return = (struct FwNamed){.label = "hello",
                                       .code  = 42})]] struct FwNamed
    test_named_struct(void) {
    return (struct FwNamed){.label = "hello", .code = 42};
}

struct FwSolo {
    int n;
};

[[cccc::test(return = (struct FwSolo){.n = 99})]] struct FwSolo
    test_solo(void) {
    return (struct FwSolo){.n = 99};
}

union FwVal {
    int   i;
    float f;
};

[[cccc::test(return = (union FwVal){.i = 123})]] union FwVal
    test_union_field(void) {
    return (union FwVal){.i = 123};
}

// Union arms alias the same storage, so an *omitted* arm must not be
// treated as "expected zero" -- .f is never asserted on here. Before #489
// this bit pattern would fail: the comparator used to walk every member,
// so the omitted .f arm was read back as ~1.0f and compared against an
// implicit 0.0 expectation.
[[cccc::test(return = (union FwVal){.i = 0x3f800000})]] union FwVal
    test_union_field_bitpattern(void) {
    return (union FwVal){.i = 0x3f800000};
}

// Nested/aggregate return= fields (ticket #489, follow-up to #353): a
// struct/union field that is itself a struct, union, or array, plus
// anonymous struct members. See man/TESTING.md's Struct/union section.
struct FwInner {
    int a;
    int b;
};
struct FwOuter {
    struct FwInner p;
    int            z;
};

// The ticket's own example spelling: an explicitly-typed nested literal.
[[cccc::test(return = (struct FwOuter){.p = (struct FwInner){.a = 1, .b = 2},
                                       .z = 3})]] struct FwOuter
    test_nested_typed(void) {
    return (struct FwOuter){.p = (struct FwInner){.a = 1, .b = 2}, .z = 3};
}

// The more common spelling: a bare brace-list inherits its type from the
// member being initialized.
[[cccc::test(return = (struct FwOuter){.p = {.a = 1, .b = 2},
                                       .z = 3})]] struct FwOuter
    test_nested_bare(void) {
    return (struct FwOuter){.p = {.a = 1, .b = 2}, .z = 3};
}

struct FwL1 {
    int v;
};
struct FwL2 {
    struct FwL1 l1;
    int         w;
};
struct FwL3 {
    struct FwL2 l2;
    int         x;
};

[[cccc::test(return = (struct FwL3){.l2 = {.l1 = {.v = 5}, .w = 6},
                                    .x  = 7})]] struct FwL3
    test_nested_two_levels(void) {
    return (struct FwL3){.l2 = {.l1 = {.v = 5}, .w = 6}, .x = 7};
}

[[cccc::test(return != (struct FwOuter){.p = {.a = 9, .b = 9},
                                        .z = 0})]] struct FwOuter
    test_nested_ne(void) {
    return (struct FwOuter){.p = {.a = 1, .b = 2}, .z = 3};
}

// A nested field omitted from the literal entirely: its sub-fields are
// expected to be zero, recursively.
[[cccc::test(return = (struct FwOuter){.z = 3})]] struct FwOuter
    test_nested_omitted(void) {
    struct FwOuter r = {0};
    r.z              = 3;
    return r;
}

struct FwInnerMix {
    char  *label;
    double val;
};
struct FwOuterMix {
    struct FwInnerMix m;
    int               code;
};

[[cccc::test(return = (struct FwOuterMix){.m    = {.label = "ok", .val = 2.5},
                                          .code = 1})]] struct FwOuterMix
    test_nested_mixed_fields(void) {
    return (struct FwOuterMix){.m = {.label = "ok", .val = 2.5}, .code = 1};
}

struct FwHolder {
    union FwVal v;
    int         tag;
};

[[cccc::test(return = (struct FwHolder){.v   = {.i = 42},
                                        .tag = 1})]] struct FwHolder
    test_nested_union(void) {
    return (struct FwHolder){.v = {.i = 42}, .tag = 1};
}

struct FwArr {
    int a[5];
    int n;
};

// Trailing array elements not listed in the literal are expected to be zero.
[[cccc::test(return = (struct FwArr){.a = {1, 2, 3}, .n = 3})]] struct FwArr
    test_array_field(void) {
    struct FwArr r = {0};
    r.a[0]         = 1;
    r.a[1]         = 2;
    r.a[2]         = 3;
    r.n            = 3;
    return r;
}

struct FwPair {
    int a;
    int b;
};
struct FwArrOfStruct {
    struct FwPair items[2];
};

[[cccc::test(return =
                        (struct FwArrOfStruct){
                            .items = {(struct FwPair){.a = 1, .b = 2},
                                      (struct FwPair){
                                          .a = 3,
                                          .b = 4}}})]] struct FwArrOfStruct
    test_array_of_struct(void) {
    struct FwArrOfStruct r;
    r.items[0] = (struct FwPair){.a = 1, .b = 2};
    r.items[1] = (struct FwPair){.a = 3, .b = 4};
    return r;
}

struct FwBuf {
    char buf[8];
    int  n;
};

// A char[] member compared against a string literal: matched with C
// zero-initialization semantics (bytes past the literal's NUL expected 0).
[[cccc::test(return = (struct FwBuf){.buf = "hi", .n = 2})]] struct FwBuf
    test_array_field_string(void) {
    struct FwBuf r = {0};
    r.buf[0]       = 'h';
    r.buf[1]       = 'i';
    r.n            = 2;
    return r;
}

// Anonymous struct member: its fields (.x, .y) live directly in the outer
// struct's designator namespace, same as C's own anonymous-member lookup.
struct FwAnonS {
    struct {
        int x;
        int y;
    };
    int z;
};

[[cccc::test(return = (struct FwAnonS){.x = 1, .y = 2, .z = 3})]] struct FwAnonS
    test_anon_struct_member(void) {
    return (struct FwAnonS){.x = 1, .y = 2, .z = 3};
}

// Anonymous *union* member (ticket #960, a follow-up left over from #489):
// dropped from the original coverage because a top-level designator into
// an anonymous union member -- exactly what the compound literal below
// does -- crashed the parser itself (struct_designator() only special-
// cased anonymous struct members, not union). cmp_ret_struct_body's own
// anonymous-member handling (`if (!m->name)`) was never gated on
// TY_STRUCT/TY_UNION, so this exercises only the parser fix, not the
// comparator.
struct FwAnonU {
    union {
        int   i;
        float f;
    };
    int z;
};

[[cccc::test(return = (struct FwAnonU){.i = 1, .z = 3})]] struct FwAnonU
    test_anon_union_member(void) {
    return (struct FwAnonU){.i = 1, .z = 3};
}

// Malformed nested literal: the inner brace list is missing a value, so
// only that field's assertion is skipped (with a -Wattributes warning) --
// parsing recovers past the *matching* '}' rather than desyncing on the
// first inner '}', so the sibling `name =` attribute after it still applies.
[[cccc::test(
    return = (struct FwOuter){.p = {.a = 1, .b}, .z = 3},
           name = "malformed nested literal recovers cleanly")]] struct FwOuter
    test_nested_malformed_recovery(void) {
    return (struct FwOuter){.p = {.a = 1, .b = 2}, .z = 3};
}

// Depth cap: a compound literal nested past CCCC_RET_FIELD_MAX_DEPTH (8)
// levels produces a -Wattributes warning and the whole assertion is
// skipped (test still runs and passes -- there's simply no return=
// assertion left to check).
struct FwD0 {
    int v;
};
struct FwD1 {
    struct FwD0 a;
};
struct FwD2 {
    struct FwD1 a;
};
struct FwD3 {
    struct FwD2 a;
};
struct FwD4 {
    struct FwD3 a;
};
struct FwD5 {
    struct FwD4 a;
};
struct FwD6 {
    struct FwD5 a;
};
struct FwD7 {
    struct FwD6 a;
};
struct FwD8 {
    struct FwD7 a;
};
struct FwD9 {
    struct FwD8 a;
};

[[cccc::test(
    return =
               (struct FwD9){
                   .a =
                       {.a = {.a = {.a = {.a = {.a = {.a = {.a = {.a = {.v = 1}}}}}}}}}},
           name = "nested past max depth is skipped, not crashed")]] struct FwD9
    test_nested_too_deep(void) {
    struct FwD9 r = {0};
    return r;
}

// [from test_return_types.c]
// Return value assertions: string, float, operator forms (tickets #346, #343).
#pragma cccc suite begin "framework/return_types"

[[cccc::test(return = -1)]]
int test_return_neg_one(void) {
    return -1;
}

[[cccc::test(return = 3.14)]]
double test_return_float(void) {
    return 3.14;
}

[[cccc::test(return = 2.0)]]
double test_return_float_computed(void) {
    double x = 1.0;
    return x + 1.0;
}

[[cccc::test(return = "hello")]]
const char *test_return_str(void) {
    return "hello";
}

[[cccc::test(return = "world")]]
const char *test_return_str_world(void) {
    return "world";
}

[[cccc::test(return = 65)]]
int test_return_char_as_int(void) {
    return (int)'A';
}

#pragma cccc suite end

#pragma cccc suite begin "framework/return_ops"

[[cccc::test(return > 0)]]
int test_return_gt_zero(void) {
    return 1;
}

[[cccc::test(return >= 1)]]
int test_return_ge_one(void) {
    return 1;
}

[[cccc::test(return < 10)]]
int test_return_lt_ten(void) {
    return 5;
}

[[cccc::test(return <= 5)]]
int test_return_le_five(void) {
    return 5;
}

[[cccc::test(return != 0)]]
int test_return_ne_zero(void) {
    return 42;
}

#pragma cccc suite end
