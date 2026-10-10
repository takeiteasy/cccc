// CCCC_FLAGS: --testing
// Consolidated suite: expressions yield unqualified rvalue types, and
// source-level comma / call results are not assignable.

#pragma cccc suite begin "rvalue_qualifiers"

typedef const int cint;
struct RS {
    int m;
    int a[2];
};

static struct RS rs_make(void) {
    struct RS s = {7, {8, 9}};
    return s;
}
static const int rs_const_int(void) {
    return 5;
}
static struct RS *rs_ptr(void) {
    static struct RS s;
    return &s;
}

// 1 when `T *` is exactly `int *` (a qualified pointee would not match).
#define PLAIN_INT(e) _Generic((__typeof__(e) *)0, int *: 1, default: 0)

[[cccc::test]]
void test_binary_arith_unqualified(void) {
    const int      ci = 1;
    volatile int   vi = 1;
    _Atomic int    ai = 1;
    const unsigned cu = 1;
    AssertEq(PLAIN_INT(ci + 1), 1);
    AssertEq(PLAIN_INT(ci + ci), 1);
    AssertEq(PLAIN_INT(vi + 1), 1);
    AssertEq(PLAIN_INT(ai + 1), 1);
    AssertEq(PLAIN_INT(ci + cu), 0);
    __typeof__(cu + ci) u = 0;
    u                     = 1;
    __typeof__(ci + 1) a  = 0;
    a                     = 2;
    AssertEq(a, 2);
}

[[cccc::test]]
void test_typedef_const_operand_unqualified(void) {
    cint ci = 3;
    AssertEq(PLAIN_INT(ci + ci), 1);
    AssertEq(PLAIN_INT(+ci), 1);
}

[[cccc::test]]
void test_unary_and_shift_unqualified(void) {
    const int    ci = 1;
    volatile int vi = 1;
    AssertEq(PLAIN_INT(+ci), 1);
    AssertEq(PLAIN_INT(-ci), 1);
    AssertEq(PLAIN_INT(~ci), 1);
    AssertEq(PLAIN_INT(ci << 1), 1);
    AssertEq(PLAIN_INT(ci >> 1), 1);
    AssertEq(PLAIN_INT(+vi), 1);
}

[[cccc::test]]
void test_conditional_unqualified(void) {
    const int  ci = 1;
    int *const cp = 0;
    int        c  = 1;
    AssertEq(PLAIN_INT(c ? ci : ci), 1);
    __typeof__(c ? cp : cp) p = 0;
    int                     x = 0;
    p                         = &x;
    AssertEq(p == &x, 1);
}

[[cccc::test]]
void test_comma_cast_call_unqualified(void) {
    const int ci = 1;
    int       x  = 0;
    AssertEq(PLAIN_INT((0, ci)), 1);
    AssertEq(PLAIN_INT((const int)x), 1);
    AssertEq(PLAIN_INT(rs_const_int()), 1);
    AssertEq(PLAIN_INT(({ ci; })), 1);
}

[[cccc::test]]
void test_assignment_results_unqualified(void) {
    volatile int vi = 0;
    _Atomic int  ai = 0;
    AssertEq(PLAIN_INT(vi = 1), 1);
    AssertEq(PLAIN_INT(vi += 1), 1);
    AssertEq(PLAIN_INT(++vi), 1);
    AssertEq(PLAIN_INT(vi++), 1);
    AssertEq(PLAIN_INT(ai++), 1);
}

[[cccc::test]]
void test_inc_dec_discarded_volatile_atomic(void) {
    volatile int vi = 0;
    _Atomic int  ai = 0;
    vi++;
    ++vi;
    vi--;
    ai++;
    ++ai;
    ai--;
    AssertEq(vi, 1);
    AssertEq(ai, 1);
}

[[cccc::test]]
void test_typeof_unqual_drops_all_qualifiers(void) {
    _Atomic int        ai = 0;
    const volatile int cv = 0;
    typeof_unqual(ai) a   = 0;
    typeof_unqual(cv) c   = 0;
    AssertEq(_Generic(&a, int *: 1, default: 0), 1);
    AssertEq(_Generic(&c, int *: 1, default: 0), 1);
}

[[cccc::test]]
void test_valid_lvalue_forms_still_assignable(void) {
    int  x = 0;
    int *p = &x;
    *p     = 4;
    AssertEq(x, 4);
    AssertEq(*&(int){3}, 3);
    rs_ptr()->m = 11;
    AssertEq(rs_ptr()->m, 11);
    rs_make().a[0] = 1; // element of an array member of a returned struct
    AssertEq(rs_make().m, 7);
}

#pragma cccc suite end
