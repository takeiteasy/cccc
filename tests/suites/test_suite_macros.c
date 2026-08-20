// CCCC_FLAGS: --testing
// Consolidated suite: macros, #define, comptime macros
// Source tests: test_define, test_macro_undef_collision_584

// [from test_macro_undef_collision_584]
// Regression test for the hashmap duplicate-key bug found while investigating
// #584: a tombstoned hash slot must not allow a duplicate macro entry to
// survive a later #undef.
//
// Each pair (A, B) below is chosen so that fnv(A) == fnv(B) (mod 4096),
// hence they collide for every power-of-two macro-table capacity the
// preprocessor uses (256..4096).  The sequence
//   #define A / #define B / #undef A / (re)#define B / #undef B
// used to leave a stale duplicate of B past A's tombstone, so #ifdef B
// wrongly saw B as still defined.  If the bug regresses, one of the
// #error directives below fires and the test fails to compile.

#define MX_0   1
#define MX_813 1
#undef MX_0
#define MX_813 1
#undef MX_813
#ifdef MX_813
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_0
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_1   1
#define MX_812 1
#undef MX_1
#define MX_812 1
#undef MX_812
#ifdef MX_812
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_1
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_2   1
#define MX_811 1
#undef MX_2
#define MX_811 1
#undef MX_811
#ifdef MX_811
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_2
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_3   1
#define MX_810 1
#undef MX_3
#define MX_810 1
#undef MX_810
#ifdef MX_810
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_3
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_4   1
#define MX_817 1
#undef MX_4
#define MX_817 1
#undef MX_817
#ifdef MX_817
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_4
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_5   1
#define MX_816 1
#undef MX_5
#define MX_816 1
#undef MX_816
#ifdef MX_816
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_5
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_6   1
#define MX_815 1
#undef MX_6
#define MX_815 1
#undef MX_815
#ifdef MX_815
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_6
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_7   1
#define MX_814 1
#undef MX_7
#define MX_814 1
#undef MX_814
#ifdef MX_814
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_7
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_8    1
#define MX_2426 1
#undef MX_8
#define MX_2426 1
#undef MX_2426
#ifdef MX_2426
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_8
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_9    1
#define MX_2427 1
#undef MX_9
#define MX_2427 1
#undef MX_2427
#ifdef MX_2427
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_9
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_10  1
#define MX_571 1
#undef MX_10
#define MX_571 1
#undef MX_571
#ifdef MX_571
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_10
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_11  1
#define MX_570 1
#undef MX_11
#define MX_570 1
#undef MX_570
#ifdef MX_570
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_11
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_12  1
#define MX_573 1
#undef MX_12
#define MX_573 1
#undef MX_573
#ifdef MX_573
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_12
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_13  1
#define MX_572 1
#undef MX_13
#define MX_572 1
#undef MX_572
#ifdef MX_572
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_13
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_14  1
#define MX_575 1
#undef MX_14
#define MX_575 1
#undef MX_575
#ifdef MX_575
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_14
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_15  1
#define MX_574 1
#undef MX_15
#define MX_574 1
#undef MX_574
#ifdef MX_574
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_15
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_16  1
#define MX_577 1
#undef MX_16
#define MX_577 1
#undef MX_577
#ifdef MX_577
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_16
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_17  1
#define MX_576 1
#undef MX_17
#define MX_576 1
#undef MX_576
#ifdef MX_576
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_17
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_18  1
#define MX_579 1
#undef MX_18
#define MX_579 1
#undef MX_579
#ifdef MX_579
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_18
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_19  1
#define MX_578 1
#undef MX_19
#define MX_578 1
#undef MX_578
#ifdef MX_578
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_19
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_20   1
#define MX_3252 1
#undef MX_20
#define MX_3252 1
#undef MX_3252
#ifdef MX_3252
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_20
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_21   1
#define MX_3253 1
#undef MX_21
#define MX_3253 1
#undef MX_3253
#ifdef MX_3253
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_21
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_22   1
#define MX_3250 1
#undef MX_22
#define MX_3250 1
#undef MX_3250
#ifdef MX_3250
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_22
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_23   1
#define MX_3251 1
#undef MX_23
#define MX_3251 1
#undef MX_3251
#ifdef MX_3251
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_23
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_24   1
#define MX_3256 1
#undef MX_24
#define MX_3256 1
#undef MX_3256
#ifdef MX_3256
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_24
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_25   1
#define MX_3257 1
#undef MX_25
#define MX_3257 1
#undef MX_3257
#ifdef MX_3257
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_25
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_26  1
#define MX_569 1
#undef MX_26
#define MX_569 1
#undef MX_569
#ifdef MX_569
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_26
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_27  1
#define MX_568 1
#undef MX_27
#define MX_568 1
#undef MX_568
#ifdef MX_568
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_27
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_28  1
#define MX_567 1
#undef MX_28
#define MX_567 1
#undef MX_567
#ifdef MX_567
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_28
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_29  1
#define MX_566 1
#undef MX_29
#define MX_566 1
#undef MX_566
#ifdef MX_566
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_29
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_30  1
#define MX_591 1
#undef MX_30
#define MX_591 1
#undef MX_591
#ifdef MX_591
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_30
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_31  1
#define MX_590 1
#undef MX_31
#define MX_590 1
#undef MX_590
#ifdef MX_590
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_31
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_32  1
#define MX_593 1
#undef MX_32
#define MX_593 1
#undef MX_593
#ifdef MX_593
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_32
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_33  1
#define MX_592 1
#undef MX_33
#define MX_592 1
#undef MX_592
#ifdef MX_592
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_33
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_34  1
#define MX_595 1
#undef MX_34
#define MX_595 1
#undef MX_595
#ifdef MX_595
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_34
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_35  1
#define MX_594 1
#undef MX_35
#define MX_594 1
#undef MX_594
#ifdef MX_594
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_35
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_36  1
#define MX_597 1
#undef MX_36
#define MX_597 1
#undef MX_597
#ifdef MX_597
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_36
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_37  1
#define MX_596 1
#undef MX_37
#define MX_596 1
#undef MX_596
#ifdef MX_596
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_37
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_38  1
#define MX_599 1
#undef MX_38
#define MX_599 1
#undef MX_599
#ifdef MX_599
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_38
#error "#584 regression: macro still defined after #undef"
#endif

#define MX_39  1
#define MX_598 1
#undef MX_39
#define MX_598 1
#undef MX_598
#ifdef MX_598
#error "#584 regression: macro still defined after #undef"
#endif
#ifdef MX_39
#error "#584 regression: macro still defined after #undef"
#endif

#pragma cccc suite begin "macros"

// test_define
[[cccc::test(return = 42)]]
int test_define(void) {
    int result = 0;
#ifdef TEST_FLAG
    result = 100;
#else
    result = 200;
#endif
    // Test that #ifdef/#else works correctly
    // Expected: 200 (TEST_FLAG not defined)
    if (result != 200)
        return 1;
    return 42;
}

// test_macro_undef_collision_584
[[cccc::test(return = 42)]]
int test_macro_undef_collision_584(void) {
    return 42;
}

#pragma cccc suite end

// [from test_comptime_in_test_mode.c]
// Comptime macros work inside [[cccc::test]] functions.
#pragma cccc suite begin "macros/comptime_in_test_mode"

[[cccc::comptime]]
int ct_mul_test_mode(int a, int b) {
    return a * b;
}

[[cccc::comptime]]
Node *ct_answer_test_mode(void) {
    return MakeIntLiteral(ct_mul_test_mode(6, 7));
}

[[cccc::comptime]]
Node *ct_two_test_mode(void) {
    return MakeIntLiteral(ct_mul_test_mode(1, 2));
}

[[cccc::test]]
void test_comptime_inline_in_test(void) {
    AssertEq(ct_answer_test_mode(), 42);
    AssertEq(ct_two_test_mode(), 2);
}

#pragma cccc suite end

// [#613] Comptime Node*-returning functions in global variable initializers.
// Before #613 these produced "not a compile-time constant (expression)".
// The macro call is now deferred to cc_finalize_macro_gvar_inits (called from
// cc_expand_macros after full parsing), so $symbol forward-refs in other macros
// are not broken.
#pragma cccc suite begin "macros/comptime_gvar_init_613"

[[cccc::comptime]]
Node *ct_gvar_val_613(void) {
    return MakeIntLiteral(42);
}

[[cccc::comptime]]
Node *ct_gvar_add_613(Node *a, Node *b) {
    return MakeBinary(NK_ADD, a, b);
}

// Simple scalar int global initialized by a comptime function.
static int ct_gvar_simple = ct_gvar_val_613();

// Scalar global initialized by a macro arithmetic expression.
static int ct_gvar_arith = ct_gvar_add_613(20, 22);

// Float scalar global initialized by a comptime function.
static float ct_gvar_float = ct_gvar_val_613();

// Macro call nested inside an arithmetic expression in the initializer.
static int ct_gvar_nested = ct_gvar_add_613(1, 1) + 40;

// $symbol forward-reference: 'fwd_613' is declared AFTER uses_fwd_613 is
// defined but the deferred path compiles macros after full parsing, so the
// $fwd_613 lookup succeeds at execute time (#613 correctness invariant).
[[cccc::comptime]]
Node *uses_fwd_613(void) {
    Obj *o = $fwd_613;
    return MakeIntLiteral(o ? 42 : 1);
}

static int fwd_613 = 7;

[[cccc::test]]
void test_comptime_gvar_init_613(void) {
    AssertEq(ct_gvar_simple, 42);
    AssertEq(ct_gvar_arith, 42);
    AssertEq((int)ct_gvar_float, 42);
    AssertEq(ct_gvar_nested, 42);
    AssertEq(uses_fwd_613(), 42);
    AssertEq(fwd_613, 7);
}

#pragma cccc suite end
