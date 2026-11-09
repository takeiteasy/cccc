// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Which expressions have an address: variables, dereferences, VLA elements and
// members of them (and of compound literals) do; calls, casts, commas and
// arithmetic do not.

#pragma cccc suite begin "lvalue_predicate"

struct LP {
    int a;
    struct {
        int b;
        int c;
    } in;
    unsigned bit : 3;
};

static struct LP lp_make(void) {
    struct LP s = {1, {2, 3}, 5};
    return s;
}

[[cccc::test]]
void test_address_of_variable_member_chain(void) {
    struct LP s = {1, {2, 3}, 0};
    int      *p = &s.in.c;
    *p          = 9;
    AssertEq(s.in.c, 9);
}

[[cccc::test]]
void test_address_of_deref_member(void) {
    struct LP  s  = {1, {2, 3}, 0};
    struct LP *ps = &s;
    int       *p  = &(*ps).in.b;
    *p            = 8;
    AssertEq(s.in.b, 8);
}

[[cccc::test]]
void test_address_of_vla_element(void) {
    int n = 4;
    int v[n];
    for (int i = 0; i < n; i++)
        v[i] = i;
    int *p = &v[2];
    *p     = 20;
    AssertEq(v[2], 20);
}

[[cccc::test]]
void test_address_of_compound_literal_member(void) {
    int *p = &((struct LP){1, {2, 3}, 0}).in.c;
    AssertEq(*p, 3);
}

[[cccc::test(expect_compile_error = true,
             error                = "cannot take the address of an rvalue")]]
void test_address_of_call_member(void) {
    int *p = &lp_make().in.b;
    (void)p;
}

[[cccc::test(expect_compile_error = true,
             error                = "cannot take the address of an rvalue")]]
void test_address_of_cast(void) {
    long x = 1;
    int *p = &(int)x;
    (void)p;
}

[[cccc::test(expect_compile_error = true,
             error                = "cannot take the address of an rvalue")]]
void test_address_of_comma(void) {
    int x  = 0;
    int *p = &(x, x);
    (void)p;
}

[[cccc::test(expect_compile_error = true,
             error                = "expression is not assignable")]]
void test_assign_call_member(void) {
    lp_make().in.b = 1;
}

#pragma cccc suite end
